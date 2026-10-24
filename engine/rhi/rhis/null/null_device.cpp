#include "rhi/rhis/null/null_device.h"

#include "core/assert/assert.h"
#include "core/containers/slot_map.h"
#include "core/log/log.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Lmao::Rhi::Null
{
    namespace
    {

        spdlog::logger *log_channel()
        {
            static spdlog::logger *channel = Log::Channel("null");
            return channel;
        }

        struct Buffer
        {
            uint64_t Size = 0;
            BufferUsage Usage = BufferUsage::None;
            std::vector<std::byte> Mapped; // Upload / Readback buffers only
        };

        struct Texture
        {
            uint32_t Width = 1;
            uint32_t Height = 1;
            uint32_t MipLevels = 1;
            Format TextureFormat = Format::Unknown;
            TextureUsage Usage = TextureUsage::None;
            bool OwnedBySwapchain = false;
        };

        struct Sampler
        {
        };

        struct Pipeline
        {
        };

        uint64_t mip_bytes(const Texture &texture, uint32_t mip)
        {
            return uint64_t{std::max(texture.Width >> mip, 1u)} * std::max(texture.Height >> mip, 1u) *
                   BytesPerTexel(texture.TextureFormat);
        }

        class NullCommandList final : public CommandList
        {
        public:
            void BeginRendering(const RenderingDesc &) override
            {
                LMAO_ASSERT(!mRendering, "BeginRendering inside BeginRendering");
                mRendering = true;
            }
            void EndRendering() override
            {
                LMAO_ASSERT(mRendering, "EndRendering without BeginRendering");
                mRendering = false;
            }
            void SetViewport(const Viewport &) override {}
            void SetScissor(const Rect &) override {}
            void BindPipeline(PipelineHandle) override {}
            void PushConstants(std::span<const std::byte> data, uint32_t offset) override
            {
                LMAO_ASSERT(offset % 4 == 0 && data.size() % 4 == 0 && offset + data.size() <= MaxPushConstantsSize,
                            "PushConstants: {} bytes at offset {} (multiples of 4, at most {} in total)", data.size(),
                            offset, MaxPushConstantsSize);
            }
            void SetFrameConstants(std::span<const std::byte> data) override
            {
                LMAO_ASSERT(data.size() <= MaxFrameConstantsSize, "SetFrameConstants: {} bytes, at most {}",
                            data.size(), MaxFrameConstantsSize);
            }
            void Draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
            void BindIndexBuffer(BufferHandle, IndexType, uint64_t) override {}
            void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
            void UploadBuffer(BufferHandle, std::span<const std::byte>, uint64_t) override {}
            void UploadTexture(TextureHandle, uint32_t, std::span<const std::byte>) override {}
            void GenerateMips(TextureHandle) override {}
            void CopyBuffer(BufferHandle, uint64_t, BufferHandle, uint64_t, uint64_t) override {}
            void CopyTextureToBuffer(TextureHandle, uint32_t, BufferHandle, uint64_t) override {}
            void BeginMarker(std::string_view) override { ++mMarkerDepth; }
            void EndMarker() override
            {
                LMAO_ASSERT(mMarkerDepth > 0, "EndMarker without BeginMarker");
                --mMarkerDepth;
            }

            // At EndFrame: what the Vulkan validation layers would catch.
            void CheckFrameEnd()
            {
                LMAO_ASSERT(!mRendering, "EndFrame inside BeginRendering");
                LMAO_ASSERT(mMarkerDepth == 0, "{} debug marker(s) still open at EndFrame", mMarkerDepth);
                mRendering = false;
                mMarkerDepth = 0;
            }

        private:
            bool mRendering = false;
            int mMarkerDepth = 0;
        };

        class NullDevice;

        class NullSwapchain final : public Swapchain
        {
        public:
            NullSwapchain(NullDevice &device, const SwapchainDesc &desc);
            ~NullSwapchain() override;

            void Resize(uint32_t width, uint32_t height) override;
            void SetVSync(bool) override {}
            UVec2 GetSize() const override { return mSize; }
            Format GetFormat() const override { return Format::BGRA8Srgb; }
            uint32_t GetImageCount() const override { return static_cast<uint32_t>(mImages.size()); }
            bool IsAcquired() const override { return mAcquired; }
            TextureHandle GetCurrentTexture() const override
            {
                LMAO_ASSERT(mAcquired, "GetCurrentTexture: the swapchain wasn't acquired this frame");
                return mImages[mCurrent];
            }

            // BeginFrame / EndFrame.
            bool Acquire();
            void Release() { mAcquired = false; }

        private:
            void create_images();
            void destroy_images();

            NullDevice &mDevice;
            UVec2 mSize;
            std::vector<TextureHandle> mImages;
            uint32_t mCurrent = 0;
            bool mAcquired = false;
        };

        class NullDevice final : public Device
        {
        public:
            NullDevice()
            {
                mInfo = {
                    .Name = "Null device",
                    .Type = AdapterType::Cpu,
                    .ApiVersion = "-",
                    .Driver = "null backend (records nothing)",
                };
                // Index 0 of every bindless array is the fallback resource (decision 0010): take
                // it here so handle indices match the Vulkan backend's.
                mBuffers.Insert({.Size = 16, .Usage = BufferUsage::Storage});
                mTextures.Insert({.TextureFormat = Format::RGBA8Unorm, .Usage = TextureUsage::Sampled});
                mSamplers.Insert({});
            }

            ~NullDevice() override
            {
                LMAO_ASSERT(mSwapchainCount == 0, "{} swapchain(s) still alive when the device was destroyed", mSwapchainCount);
                LMAO_ASSERT(mTextures.Size() == 1, "{} texture(s) still alive when the device was destroyed", mTextures.Size() - 1);
                LMAO_ASSERT(mBuffers.Size() == 1, "{} buffer(s) still alive when the device was destroyed", mBuffers.Size() - 1);
                LMAO_ASSERT(mSamplers.Size() == 1, "{} sampler(s) still alive when the device was destroyed", mSamplers.Size() - 1);
                LMAO_ASSERT(mPipelines.IsEmpty(), "{} pipeline(s) still alive when the device was destroyed", mPipelines.Size());
                LMAO_LOG_INFO(log_channel(), "Null device destroyed after {} frame(s)", mFrameCount);
            }

            Backend GetBackend() const override { return Backend::Null; }
            const AdapterInfo &GetAdapterInfo() const override { return mInfo; }
            void WaitIdle() override {}

            std::unique_ptr<Swapchain> CreateSwapchain(const SwapchainDesc &desc) override
            {
                return std::make_unique<NullSwapchain>(*this, desc);
            }

            BufferHandle CreateBuffer(const BufferDesc &desc) override
            {
                if (desc.Size == 0 || desc.Usage == BufferUsage::None)
                {
                    LMAO_LOG_ERROR(log_channel(), "CreateBuffer '{}': size and usage must be non-zero", desc.DebugName);
                    return {};
                }
                Buffer buffer{.Size = desc.Size, .Usage = desc.Usage};
                if (desc.Memory != MemoryUsage::GpuOnly)
                    buffer.Mapped.resize(desc.Size); // real memory: callers write through GetMappedData
                return mBuffers.Insert(std::move(buffer));
            }
            void DestroyBuffer(BufferHandle buffer) override { destroy(mBuffers, buffer, "DestroyBuffer"); }
            void *GetMappedData(BufferHandle handle) override
            {
                Buffer &buffer = mBuffers[handle];
                return buffer.Mapped.empty() ? nullptr : buffer.Mapped.data();
            }

            TextureHandle CreateTexture(const TextureDesc &desc) override
            {
                if (desc.Width == 0 || desc.Height == 0 || desc.Format == Format::Unknown || desc.Usage == TextureUsage::None)
                {
                    LMAO_LOG_ERROR(log_channel(), "CreateTexture '{}': size, format and usage must be set", desc.DebugName);
                    return {};
                }
                const uint32_t full = FullMipCount(desc.Width, desc.Height);
                return mTextures.Insert({
                    .Width = desc.Width,
                    .Height = desc.Height,
                    .MipLevels = desc.MipLevels == 0 ? full : std::min(desc.MipLevels, full),
                    .TextureFormat = desc.Format,
                    .Usage = desc.Usage,
                });
            }
            void DestroyTexture(TextureHandle texture) override
            {
                const Texture *found = mTextures.Get(texture);
                LMAO_ASSERT(!found || !found->OwnedBySwapchain, "DestroyTexture: swapchain images belong to their swapchain");
                destroy(mTextures, texture, "DestroyTexture");
            }

            SamplerHandle CreateSampler(const SamplerDesc &) override { return mSamplers.Insert({}); }
            void DestroySampler(SamplerHandle sampler) override { destroy(mSamplers, sampler, "DestroySampler"); }

            MemoryStats GetMemoryStats() const override
            {
                return {.AllocationCount = static_cast<uint32_t>(mBuffers.Size() + mTextures.Size())};
            }

            // Queued work completes immediately: there is nothing to record it into.
            void QueueUpload(BufferHandle handle, std::vector<std::byte> data, uint64_t offset) override
            {
                const Buffer *buffer = mBuffers.Get(handle);
                LMAO_ASSERT(buffer, "QueueUpload: stale or invalid buffer");
                if (!buffer)
                    return;
                LMAO_ASSERT(HasAny(buffer->Usage, BufferUsage::CopyDest), "QueueUpload: the buffer has no CopyDest usage");
                LMAO_ASSERT(offset + data.size() <= buffer->Size, "QueueUpload: {} bytes at offset {} overflow a {}-byte buffer",
                            data.size(), offset, buffer->Size);
            }
            void QueueUpload(TextureHandle handle, uint32_t mip, std::vector<std::byte> data) override
            {
                const Texture *texture = mTextures.Get(handle);
                LMAO_ASSERT(texture, "QueueUpload: stale or invalid texture");
                if (!texture)
                    return;
                LMAO_ASSERT(HasAny(texture->Usage, TextureUsage::CopyDest), "QueueUpload: the texture has no CopyDest usage");
                LMAO_ASSERT(!IsDepthFormat(texture->TextureFormat), "QueueUpload: depth textures can't be uploaded");
                LMAO_ASSERT(mip < texture->MipLevels && data.size() == mip_bytes(*texture, mip),
                            "QueueUpload: mip {} of a {}-mip texture needs {} bytes, got {}", mip, texture->MipLevels,
                            mip < texture->MipLevels ? mip_bytes(*texture, mip) : 0, data.size());
            }
            void QueueGenerateMips(TextureHandle handle) override
            {
                LMAO_ASSERT(mTextures.Contains(handle), "QueueGenerateMips: stale or invalid texture");
            }
            bool IsUploadPending(BufferHandle) const override { return false; }
            bool IsUploadPending(TextureHandle) const override { return false; }
            TextureIndex GetTextureIndex(TextureHandle texture, TextureHandle placeholder) const override
            {
                return DescriptorIndex(mTextures.Contains(texture) ? texture : placeholder);
            }
            uint64_t GetPendingUploadBytes() const override { return 0; }

            PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override
            {
                if (desc.Vertex.Code.empty() || (desc.Fragment.Code.empty() && !desc.ColorFormats.empty()) ||
                    (!desc.Blend.empty() && desc.Blend.size() != desc.ColorFormats.size()))
                {
                    LMAO_LOG_ERROR(log_channel(), "CreateGraphicsPipeline '{}': invalid description", desc.DebugName);
                    return {};
                }
                return mPipelines.Insert({});
            }
            void DestroyPipeline(PipelineHandle pipeline) override { destroy(mPipelines, pipeline, "DestroyPipeline", false); }
            bool ReplacePipeline(PipelineHandle pipeline, const GraphicsPipelineDesc &) override
            {
                LMAO_ASSERT(mPipelines.Contains(pipeline), "ReplacePipeline: stale or invalid handle");
                return mPipelines.Contains(pipeline);
            }

            CommandList *BeginFrame(std::span<Swapchain *const> swapchains) override
            {
                LMAO_ASSERT(!mRecording, "BeginFrame called twice without EndFrame");
                mFrameSwapchains.clear();
                for (Swapchain *swapchain : swapchains)
                {
                    auto *null_swapchain = static_cast<NullSwapchain *>(swapchain);
                    if (null_swapchain->Acquire())
                        mFrameSwapchains.push_back(null_swapchain);
                }
                if (!swapchains.empty() && mFrameSwapchains.empty())
                    return nullptr; // every swapchain is minimized
                mRecording = true;
                return &mCommandList;
            }
            using Device::BeginFrame;

            void EndFrame() override
            {
                LMAO_ASSERT(mRecording, "EndFrame without a successful BeginFrame");
                mCommandList.CheckFrameEnd();
                for (NullSwapchain *swapchain : mFrameSwapchains)
                    swapchain->Release();
                mFrameSwapchains.clear();
                mRecording = false;
                ++mFrameCount;
            }

            // For NullSwapchain.
            TextureHandle CreateSwapchainImage(UVec2 size)
            {
                return mTextures.Insert({
                    .Width = size.x,
                    .Height = size.y,
                    .TextureFormat = Format::BGRA8Srgb,
                    .Usage = TextureUsage::ColorAttachment | TextureUsage::CopySource,
                    .OwnedBySwapchain = true,
                });
            }
            void DestroySwapchainImage(TextureHandle image) { mTextures.Remove(image); }
            void OnSwapchainCreated() { ++mSwapchainCount; }
            void OnSwapchainDestroyed() { --mSwapchainCount; }
            bool IsRecording() const { return mRecording; }

        private:
            // has_fallback: index 0 is the device's fallback resource and can't be destroyed.
            template <typename Map, typename HandleType>
            static void destroy(Map &map, HandleType handle, const char *what, bool has_fallback = true)
            {
                const bool valid = map.Contains(handle) && !(has_fallback && handle.Index == 0);
                LMAO_ASSERT(valid, "{}: stale or invalid handle", what);
                if (valid)
                    map.Remove(handle);
            }

            AdapterInfo mInfo;
            SlotMap<Buffer, BufferTag> mBuffers;
            SlotMap<Texture, TextureTag> mTextures;
            SlotMap<Sampler, SamplerTag> mSamplers;
            SlotMap<Pipeline, PipelineTag> mPipelines;
            NullCommandList mCommandList;
            std::vector<NullSwapchain *> mFrameSwapchains;
            bool mRecording = false;
            uint32_t mSwapchainCount = 0;
            uint64_t mFrameCount = 0;
        };

        NullSwapchain::NullSwapchain(NullDevice &device, const SwapchainDesc &desc)
            : mDevice(device), mSize(desc.Width, desc.Height)
        {
            mDevice.OnSwapchainCreated();
            create_images();
        }

        NullSwapchain::~NullSwapchain()
        {
            LMAO_ASSERT(!mDevice.IsRecording(), "a swapchain was destroyed between BeginFrame and EndFrame");
            destroy_images();
            mDevice.OnSwapchainDestroyed();
        }

        void NullSwapchain::Resize(uint32_t width, uint32_t height)
        {
            if (mSize == UVec2(width, height))
                return;
            mSize = UVec2(width, height);
            // Recreated right away (Vulkan waits for the next BeginFrame; nothing here can be in use).
            destroy_images();
            create_images();
        }

        bool NullSwapchain::Acquire()
        {
            if (mImages.empty())
                return false; // 0 x 0: minimized
            mCurrent = (mCurrent + 1) % static_cast<uint32_t>(mImages.size());
            mAcquired = true;
            return true;
        }

        void NullSwapchain::create_images()
        {
            if (mSize.x == 0 || mSize.y == 0)
                return;
            for (uint32_t i = 0; i < Device::MaxFramesInFlight + 1; ++i)
                mImages.push_back(mDevice.CreateSwapchainImage(mSize));
        }

        void NullSwapchain::destroy_images()
        {
            for (TextureHandle image : mImages)
                mDevice.DestroySwapchainImage(image);
            mImages.clear();
            mCurrent = 0;
        }

    } // namespace

    std::unique_ptr<Device> CreateDevice(const DeviceDesc &)
    {
        LMAO_LOG_INFO(log_channel(), "Using the null RHI backend: nothing is rendered");
        return std::make_unique<NullDevice>();
    }

} // namespace Lmao::Rhi::Null
