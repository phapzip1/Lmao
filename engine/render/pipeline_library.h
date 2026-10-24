#pragma once

// Creates pipelines from embedded SPIR-V and, in development builds (LMAO_SHADER_HOT_RELOAD),
// rebuilds them when their Slang source changes. Callers keep their handles: a reloaded
// pipeline is swapped in behind the same handle (Device::ReplacePipeline).
//
//   Render::PipelineLibrary pipelines(*device);
//   const auto code = std::as_bytes(std::span(Shaders::Model));
//   auto handle = pipelines.CreateGraphicsPipeline({.Vertex = {code, "vertexMain"}, ...}, Shaders::ModelSource);
//   ...
//   pipelines.Update(); // once per frame
//
// Decision 0011.

#include "rhi/device.h"

#include <memory>
#include <string_view>

namespace Lmao::Render
{

    class PipelineLibrary
    {
    public:
        explicit PipelineLibrary(Rhi::Device &device);
        // Destroys the pipelines still in the library and waits for compiles in flight.
        ~PipelineLibrary();

        PipelineLibrary(const PipelineLibrary &) = delete;
        PipelineLibrary &operator=(const PipelineLibrary &) = delete;

        // Creates the pipeline from desc as Device::CreateGraphicsPipeline does. With hot reload,
        // desc is remembered and both stages are recompiled from source_path (a generated
        // Shaders::<Name>Source) whenever it or a file it imports changes. An empty source_path
        // never reloads. The library owns the pipeline: destroy it here, not on the device.
        Rhi::PipelineHandle CreateGraphicsPipeline(const Rhi::GraphicsPipelineDesc &desc, std::string_view source_path);
        void DestroyPipeline(Rhi::PipelineHandle pipeline);

        // About twice a second, checks shader files for changes and starts recompiling the
        // sources that use them (on a background thread). Swaps in pipelines whose compile has
        // finished; on a compile error the old pipeline stays and the error is logged. Call once
        // per frame from the thread that uses the device. Does nothing without hot reload.
        void Update();

        bool IsHotReloadEnabled() const;
        uint32_t GetReloadCount() const; // successful reloads, for UI

    private:
        struct State;
        Rhi::Device &mDevice;
        std::unique_ptr<State> mState; // hot reload bookkeeping; null without LMAO_SHADER_HOT_RELOAD
    };

} // namespace Lmao::Render
