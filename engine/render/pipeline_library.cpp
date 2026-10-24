#include "render/pipeline_library.h"

#include "core/assert/assert.h"

#include <algorithm>
#include <vector>

#if LMAO_SHADER_HOT_RELOAD
#include "core/io/path.h"
#include "core/log/log.h"
#include "core/platform/process.h"
#include "core/profile/profile.h"
#include "render/slangc_config.h" // generated: slangc path, flags and include directory

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <string>
#include <system_error>
#include <unordered_map>
#endif

namespace Lmao::Render
{
    namespace
    {

#if LMAO_SHADER_HOT_RELOAD
        spdlog::logger *log_channel()
        {
            static spdlog::logger *channel = Log::Channel("render");
            return channel;
        }

        using Clock = std::chrono::steady_clock;
        constexpr auto PollInterval = std::chrono::milliseconds(500);

        // A GraphicsPipelineDesc that owns what the original only pointed at.
        struct OwnedPipelineDesc
        {
            std::string vertex_entry;
            std::string fragment_entry; // empty: depth-only
            std::vector<Rhi::Format> color_formats;
            std::vector<Rhi::BlendState> blend;
            Rhi::Format depth_format = Rhi::Format::Unknown;
            Rhi::DepthState depth;
            Rhi::PrimitiveTopology topology = Rhi::PrimitiveTopology::TriangleList;
            Rhi::CullMode cull = Rhi::CullMode::Back;
            std::string debug_name;

            explicit OwnedPipelineDesc(const Rhi::GraphicsPipelineDesc &desc)
                : vertex_entry(desc.Vertex.EntryPoint),
                  fragment_entry(desc.Fragment.Code.empty() ? std::string_view() : desc.Fragment.EntryPoint),
                  color_formats(desc.ColorFormats.begin(), desc.ColorFormats.end()),
                  blend(desc.Blend.begin(), desc.Blend.end()), depth_format(desc.DepthFormat), depth(desc.Depth),
                  topology(desc.Topology), cull(desc.Cull), debug_name(desc.DebugName)
            {
            }

            // Both stages from one module, as lmao_add_shaders compiles every entry point together.
            Rhi::GraphicsPipelineDesc With(std::span<const std::byte> code) const
            {
                return {
                    .Vertex = {.Code = code, .EntryPoint = vertex_entry},
                    .Fragment = fragment_entry.empty() ? Rhi::ShaderStage{} : Rhi::ShaderStage{code, fragment_entry},
                    .ColorFormats = color_formats,
                    .Blend = blend,
                    .DepthFormat = depth_format,
                    .Depth = depth,
                    .Topology = topology,
                    .Cull = cull,
                    .DebugName = debug_name,
                };
            }
        };

        struct CompileResult
        {
            bool succeeded = false;
            std::string output; // compiler messages
            std::vector<std::byte> code;
            std::vector<std::filesystem::path> dependencies; // from the depfile; empty if unknown
            double milliseconds = 0.0;
        };

        std::vector<std::byte> read_binary(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return {};
            const std::streamoff size = file.tellg();
            std::vector<std::byte> data(size > 0 ? static_cast<size_t>(size) : 0);
            file.seekg(0);
            file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
            return file ? data : std::vector<std::byte>();
        }

        // Make-style depfile: "target: dep dep \<newline> dep", spaces in paths escaped as "\ ".
        std::vector<std::filesystem::path> parse_depfile(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            // The target ends at the first ": " (a Windows drive colon is followed by a slash).
            const size_t colon = text.find(": ");
            if (colon == std::string::npos)
                return {};
            std::vector<std::filesystem::path> dependencies;
            std::string current;
            for (size_t i = colon + 2; i < text.size(); ++i)
            {
                const char c = text[i];
                if (c == '\\' && i + 1 < text.size() && (text[i + 1] == ' ' || text[i + 1] == '\n' || text[i + 1] == '\r'))
                {
                    if (text[i + 1] == ' ')
                        current += ' ';
                    ++i;
                    continue;
                }
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                {
                    if (!current.empty())
                        dependencies.push_back(PathFromUtf8(current).lexically_normal());
                    current.clear();
                    continue;
                }
                current += c;
            }
            if (!current.empty())
                dependencies.push_back(PathFromUtf8(current).lexically_normal());
            return dependencies;
        }

        // Runs on a worker thread: touches no device or library state.
        CompileResult compile(std::filesystem::path source, std::filesystem::path output)
        {
            const auto start = Clock::now();
            std::filesystem::path depfile = output;
            depfile += ".d";
            std::vector<std::string> command{std::string(Generated::SlangcPath), PathToUtf8(source)};
            for (const std::string_view flag : Generated::SlangcFlags)
                command.emplace_back(flag);
            command.insert(command.end(), {"-I", std::string(Generated::ShaderIncludeDirectory)});
#if defined(NDEBUG)
            command.emplace_back("-O2");
#else
            command.insert(command.end(), {"-g", "-O0"});
#endif
            command.insert(command.end(), {"-depfile", PathToUtf8(depfile), "-o", PathToUtf8(output)});

            const ProcessResult process = RunProcess(command);
            CompileResult result;
            result.output = process.Output;
            result.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (!process.Started || process.ExitCode != 0)
                return result;
            result.code = read_binary(output);
            result.dependencies = parse_depfile(depfile);
            result.succeeded = !result.code.empty() && result.code.size() % 4 == 0;
            if (!result.succeeded)
                result.output += "\n(no SPIR-V module was written)";
            return result;
        }

        std::filesystem::file_time_type modification_time(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto time = std::filesystem::last_write_time(path, error);
            return error ? std::filesystem::file_time_type::min() : time;
        }
#endif

    } // namespace

    struct PipelineLibrary::State
    {
        std::vector<Rhi::PipelineHandle> owned; // destroyed with the library

#if LMAO_SHADER_HOT_RELOAD
        struct Pipeline
        {
            Rhi::PipelineHandle handle;
            OwnedPipelineDesc desc;
            size_t source = 0; // into sources
        };

        struct Source
        {
            std::filesystem::path path;
            std::string name; // file name, for logs and the temporary output
            // Every file the compile read, from the last depfile. Until the first runtime compile
            // it's unknown, and a change to any watched file recompiles this source.
            std::vector<std::filesystem::path> dependencies;
            bool dirty = false; // changed while a compile was running: compile again after it
            std::future<CompileResult> compile;
        };

        std::vector<Pipeline> pipelines;
        std::vector<Source> sources;
        std::unordered_map<std::string, std::filesystem::file_time_type> times; // watched file -> last seen
        std::filesystem::path output_directory; // per process, removed with the library
        Clock::time_point next_poll{};
        uint32_t reloads = 0;

        size_t find_or_add_source(std::string_view source_path)
        {
            const std::filesystem::path path = PathFromUtf8(source_path).lexically_normal();
            for (size_t i = 0; i < sources.size(); ++i)
                if (sources[i].path == path)
                    return i;
            sources.push_back({.path = path, .name = PathToUtf8(path.filename())});
            return sources.size() - 1;
        }

        // The sources' own files plus every .slang file in the shader include directory (where
        // shared modules live). Files seen for the first time are recorded, not treated as changed.
        std::vector<std::filesystem::path> scan_for_changes()
        {
            std::vector<std::filesystem::path> files;
            for (const Source &source : sources)
                files.push_back(source.path);
            for (const Source &source : sources)
                files.insert(files.end(), source.dependencies.begin(), source.dependencies.end());
            std::error_code error;
            for (auto it = std::filesystem::recursive_directory_iterator(PathFromUtf8(Generated::ShaderIncludeDirectory), error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                if (it->path().extension() == ".slang")
                    files.push_back(it->path().lexically_normal());

            std::vector<std::filesystem::path> changed;
            for (const std::filesystem::path &file : files)
            {
                const auto time = modification_time(file);
                if (time == std::filesystem::file_time_type::min())
                    continue; // missing, or mid-save by an editor that replaces the file
                const auto [entry, inserted] = times.try_emplace(PathToUtf8(file), time);
                if (!inserted && entry->second != time)
                {
                    entry->second = time;
                    if (std::find(changed.begin(), changed.end(), file) == changed.end())
                        changed.push_back(file);
                }
            }
            return changed;
        }

        void start_compile(Source &source)
        {
            source.dirty = false;
            LMAO_LOG_INFO(log_channel(), "Shader hot reload: recompiling {}", source.name);
            source.compile = std::async(std::launch::async, compile, source.path,
                                        output_directory / (source.name + ".spv"));
        }
#endif
    };

    PipelineLibrary::PipelineLibrary(Rhi::Device &device) : mDevice(device), mState(std::make_unique<State>())
    {
#if LMAO_SHADER_HOT_RELOAD
        std::random_device random;
        std::error_code error;
        mState->output_directory = std::filesystem::temp_directory_path(error) /
                                   fmt::format("lmao-shaders-{:08x}", static_cast<uint32_t>(random()));
        std::filesystem::create_directories(mState->output_directory, error);
        LMAO_LOG_INFO(log_channel(), "Shader hot reload on: watching '{}' with {}", Generated::ShaderIncludeDirectory,
                      Generated::SlangcPath);
#endif
    }

    PipelineLibrary::~PipelineLibrary()
    {
#if LMAO_SHADER_HOT_RELOAD
        for (State::Source &source : mState->sources)
            if (source.compile.valid())
                source.compile.wait();
        std::error_code error;
        std::filesystem::remove_all(mState->output_directory, error);
#endif
        for (const Rhi::PipelineHandle pipeline : mState->owned)
            mDevice.DestroyPipeline(pipeline);
    }

    Rhi::PipelineHandle PipelineLibrary::CreateGraphicsPipeline(const Rhi::GraphicsPipelineDesc &desc,
                                                                std::string_view source_path)
    {
        const Rhi::PipelineHandle handle = mDevice.CreateGraphicsPipeline(desc);
        if (!handle)
            return {};
        mState->owned.push_back(handle);
#if LMAO_SHADER_HOT_RELOAD
        if (!source_path.empty())
        {
            const size_t source = mState->find_or_add_source(source_path);
            mState->pipelines.push_back({.handle = handle, .desc = OwnedPipelineDesc(desc), .source = source});
        }
#else
        (void)source_path;
#endif
        return handle;
    }

    void PipelineLibrary::DestroyPipeline(Rhi::PipelineHandle pipeline)
    {
        const auto owned = std::find(mState->owned.begin(), mState->owned.end(), pipeline);
        LMAO_ASSERT(owned != mState->owned.end(), "PipelineLibrary::DestroyPipeline: not a pipeline of this library");
        if (owned == mState->owned.end())
            return;
        mState->owned.erase(owned);
#if LMAO_SHADER_HOT_RELOAD
        std::erase_if(mState->pipelines, [&](const State::Pipeline &entry) { return entry.handle == pipeline; });
#endif
        mDevice.DestroyPipeline(pipeline);
    }

    void PipelineLibrary::Update()
    {
#if LMAO_SHADER_HOT_RELOAD
        LMAO_PROFILE_ZONE();
        State &state = *mState;

        // Finished compiles: swap the new code into every pipeline built from that source.
        for (size_t index = 0; index < state.sources.size(); ++index)
        {
            State::Source &source = state.sources[index];
            if (!source.compile.valid() || source.compile.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                continue;
            CompileResult result = source.compile.get();
            if (!result.succeeded)
            {
                LMAO_LOG_ERROR(log_channel(), "Shader hot reload: {} failed to compile, keeping the old pipelines:\n{}",
                               source.name, result.output);
            }
            else
            {
                source.dependencies = std::move(result.dependencies);
                uint32_t replaced = 0;
                uint32_t failed = 0;
                for (const State::Pipeline &pipeline : state.pipelines)
                    if (pipeline.source == index)
                        ++(mDevice.ReplacePipeline(pipeline.handle, pipeline.desc.With(result.code)) ? replaced : failed);
                if (failed == 0)
                    ++state.reloads;
                LMAO_LOG_INFO(log_channel(), "Shader hot reload: {} compiled in {:.0f} ms, {} pipeline(s) replaced{}",
                              source.name, result.milliseconds, replaced,
                              failed ? fmt::format(", {} failed (old ones kept)", failed) : std::string());
            }
            if (source.dirty)
                state.start_compile(source);
        }

        const Clock::time_point now = Clock::now();
        if (now < state.next_poll)
            return;
        state.next_poll = now + PollInterval;
        const std::vector<std::filesystem::path> changed = state.scan_for_changes();
        if (changed.empty())
            return;
        for (State::Source &source : state.sources)
        {
            const bool affected = std::any_of(changed.begin(), changed.end(), [&](const std::filesystem::path &file) {
                return file == source.path || source.dependencies.empty() || // unknown until the first runtime compile
                       std::find(source.dependencies.begin(), source.dependencies.end(), file) != source.dependencies.end();
            });
            if (!affected)
                continue;
            if (source.compile.valid())
                source.dirty = true; // compile again once the running one is done
            else
                state.start_compile(source);
        }
#endif
    }

    bool PipelineLibrary::IsHotReloadEnabled() const { return LMAO_SHADER_HOT_RELOAD != 0; }

    uint32_t PipelineLibrary::GetReloadCount() const
    {
#if LMAO_SHADER_HOT_RELOAD
        return mState->reloads;
#else
        return 0;
#endif
    }

} // namespace Lmao::Render
