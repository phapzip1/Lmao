#pragma once

// GPU device: the entry point to the RHI. No backend type appears in this interface; each
// backend lives in its own target under rhi/rhis/ (vulkan/, later d3d12/ and null/) and is
// chosen at runtime.
//
//     auto device = Rhi::Device::Create({.ApplicationName = "Game"});
//     LMAO_INFO("GPU: {}", device->GetAdapterInfo().Name);

#include "rhi/command_list.h"
#include "rhi/pipeline.h"
#include "rhi/resources.h"
#include "rhi/swapchain.h"
#include "rhi/types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Lmao::Rhi
{

    enum class Backend : uint8_t
    {
        Vulkan
    };

    enum class AdapterType : uint8_t
    {
        Other,
        Discrete,
        Integrated,
        Virtual,
        Cpu
    };

    struct AdapterInfo
    {
        std::string Name;
        AdapterType Type = AdapterType::Other;
        uint32_t VendorId = 0;
        uint32_t DeviceId = 0;
        std::string ApiVersion;       // highest version the GPU supports, e.g. "1.4.329"
        std::string Driver;           // driver name and version as reported by the driver
        uint64_t DedicatedMemory = 0; // bytes of GPU-local memory
    };

    struct DeviceDesc
    {
        Backend Api = Backend::Vulkan;
        std::string_view ApplicationName = "Lmao"; // UTF-8; shown in tools like RenderDoc
#if defined(NDEBUG)
        bool EnableValidation = false;
#else
        bool EnableValidation = true; // API validation layers: slow, for development only
#endif
        bool BreakOnValidationError = true; // break into an attached debugger on validation errors
        // GPU-assisted validation (needs EnableValidation): instruments shaders to catch
        // out-of-range or destroyed bindless indices and out-of-bounds buffer access. Much slower;
        // turn on while chasing a bug.
        bool GpuValidation = false;
    };

    class Device
    {
    public:
        // The CPU records at most this many frames ahead of the GPU.
        static constexpr uint32_t MaxFramesInFlight = 2;

        // Picks the best suitable GPU and logs every GPU it saw. nullptr on failure (the reason is logged).
        static std::unique_ptr<Device> Create(const DeviceDesc &desc = {});

        virtual ~Device() = default;

        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;

        virtual Backend GetBackend() const = 0;
        virtual const AdapterInfo &GetAdapterInfo() const = 0;

        // Blocks until the GPU has finished all submitted work.
        virtual void WaitIdle() = 0;

        // nullptr on failure (the reason is logged).
        virtual std::unique_ptr<Swapchain> CreateSwapchain(const SwapchainDesc &desc) = 0;

        // Resources. Create* returns an invalid handle on failure (the reason is logged).
        // Destroy* is safe while frames using the resource are in flight: destruction is deferred
        // until the GPU is done with them.
        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
        virtual void DestroyBuffer(BufferHandle buffer) = 0;
        // CPU pointer to an Upload or Readback buffer (mapped for its whole life); nullptr for GpuOnly.
        virtual void *GetMappedData(BufferHandle buffer) = 0;

        virtual TextureHandle CreateTexture(const TextureDesc &desc) = 0;
        // Only for textures from CreateTexture; swapchain images belong to their swapchain.
        virtual void DestroyTexture(TextureHandle texture) = 0;

        virtual SamplerHandle CreateSampler(const SamplerDesc &desc) = 0;
        virtual void DestroySampler(SamplerHandle sampler) = 0;

        virtual MemoryStats GetMemoryStats() const = 0;

        // An invalid handle on failure (the reason is logged).
        virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) = 0;
        // Safe to call while frames using the pipeline are in flight: destruction is deferred
        // until the GPU is done with them.
        virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

        // Waits until the GPU is done with the frame slot being reused, acquires the swapchain's
        // next image and starts recording. nullptr means "skip this frame" (minimized, or the
        // swapchain could not be recreated); don't call EndFrame then.
        virtual CommandList *BeginFrame(Swapchain &swapchain) = 0;
        // Submits the frame's commands and presents the swapchain image.
        virtual void EndFrame() = 0;

    protected:
        Device() = default;
    };

    constexpr std::string_view ToString(Backend backend)
    {
        switch (backend)
        {
        case Backend::Vulkan:
            return "Vulkan";
        }
        return "Unknown";
    }

    constexpr std::string_view ToString(AdapterType type)
    {
        switch (type)
        {
        case AdapterType::Discrete:
            return "discrete";
        case AdapterType::Integrated:
            return "integrated";
        case AdapterType::Virtual:
            return "virtual";
        case AdapterType::Cpu:
            return "CPU";
        case AdapterType::Other:
            break;
        }
        return "other";
    }

} // namespace Lmao::Rhi
