// VulkanDevice: queued uploads, recorded at the start of each frame within a byte budget.
//
// Each frame, BeginFrame takes work from the front of the queue in order until the budget is
// spent: a buffer upload is split into byte ranges, a texture mip into bands of rows, and the
// rest continues next frame. Staging comes from the per-frame upload ring, which the budget
// keeps from overflowing, so big loads neither stall one frame nor allocate one-off staging.

#include "rhi/rhis/vulkan/vulkan_device_internal.h"

#include "core/assert/assert.h"
#include "core/profile/profile.h"

#include <algorithm>

namespace Lmao::Rhi::Vulkan
{

    void VulkanDevice::QueueUpload(BufferHandle handle, std::vector<std::byte> data, uint64_t offset)
    {
        Buffer *buffer = mBuffers.Get(handle);
        LMAO_ASSERT(buffer && !buffer->Destroyed, "QueueUpload: stale, invalid or destroyed buffer");
        if (!buffer || buffer->Destroyed || data.empty())
            return;
        LMAO_ASSERT(buffer->Usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT, "QueueUpload: the buffer has no CopyDest usage");
        LMAO_ASSERT(offset + data.size() <= buffer->Size, "QueueUpload: {} bytes at offset {} overflow a {}-byte buffer",
                    data.size(), offset, buffer->Size);
        if (offset + data.size() > buffer->Size)
            return;

        ++buffer->PendingUploads;
        mQueuedBytes += data.size();
        mUploadQueue.push_back({.kind = QueuedUpload::Kind::Buffer, .buffer = handle, .offset = offset, .data = std::move(data)});
    }

    void VulkanDevice::QueueUpload(TextureHandle handle, uint32_t mip, std::vector<std::byte> data)
    {
        Texture *texture = mTextures.Get(handle);
        LMAO_ASSERT(texture && !texture->Destroyed, "QueueUpload: stale, invalid or destroyed texture");
        if (!texture || texture->Destroyed)
            return;
        LMAO_ASSERT(texture->Usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT, "QueueUpload: the texture has no CopyDest usage");
        LMAO_ASSERT(texture->Aspect == VK_IMAGE_ASPECT_COLOR_BIT, "QueueUpload: depth textures can't be uploaded");
        LMAO_ASSERT(mip < texture->MipLevels && data.size() == MipBytes(*texture, mip),
                    "QueueUpload: mip {} needs {} bytes, got {}", mip, mip < texture->MipLevels ? MipBytes(*texture, mip) : 0,
                    data.size());
        if (mip >= texture->MipLevels || data.size() != MipBytes(*texture, mip))
            return;

        ++texture->PendingUploads;
        mQueuedBytes += data.size();
        mUploadQueue.push_back({.kind = QueuedUpload::Kind::Texture, .texture = handle, .mip = mip, .data = std::move(data)});
    }

    void VulkanDevice::QueueGenerateMips(TextureHandle handle)
    {
        Texture *texture = mTextures.Get(handle);
        LMAO_ASSERT(texture && !texture->Destroyed, "QueueGenerateMips: stale, invalid or destroyed texture");
        if (!texture || texture->Destroyed)
            return;
        ++texture->PendingUploads;
        mUploadQueue.push_back({.kind = QueuedUpload::Kind::GenerateMips, .texture = handle});
    }

    bool VulkanDevice::IsUploadPending(BufferHandle handle) const
    {
        const Buffer *buffer = mBuffers.Get(handle);
        return buffer && !buffer->Destroyed && buffer->PendingUploads != 0;
    }

    bool VulkanDevice::IsUploadPending(TextureHandle handle) const
    {
        const Texture *texture = mTextures.Get(handle);
        return texture && !texture->Destroyed && texture->PendingUploads != 0;
    }

    TextureIndex VulkanDevice::GetTextureIndex(TextureHandle handle, TextureHandle placeholder) const
    {
        const Texture *texture = mTextures.Get(handle);
        if (texture && !texture->Destroyed && texture->PendingUploads == 0)
            return DescriptorIndex(handle);
        return DescriptorIndex(placeholder); // index 0 (the fallback) if the placeholder is invalid
    }

    void VulkanDevice::finish_queued(const QueuedUpload &upload)
    {
        mQueuedBytes -= upload.data.size();
        if (upload.kind == QueuedUpload::Kind::Buffer)
            --mBuffers[upload.buffer].PendingUploads;
        else
            --mTextures[upload.texture].PendingUploads;
    }

    void VulkanDevice::drop_queued(BufferHandle buffer, TextureHandle texture)
    {
        std::erase_if(mUploadQueue, [&](const QueuedUpload &upload)
                      {
                          const bool match = (buffer && upload.buffer == buffer) || (texture && upload.texture == texture);
                          if (match)
                              mQueuedBytes -= upload.data.size();
                          return match;
                      });
    }

    void VulkanDevice::drain_upload_queue()
    {
        if (mUploadQueue.empty())
            return;
        LMAO_PROFILE_ZONE();
        mCommandList.BeginMarker("Queued uploads");

        uint64_t budget = mUploadBudget;
        while (!mUploadQueue.empty())
        {
            QueuedUpload &upload = mUploadQueue.front();
            bool done = false;
            switch (upload.kind)
            {
            case QueuedUpload::Kind::GenerateMips:
                // GPU work, no staging: not counted against the budget.
                mCommandList.GenerateMips(upload.texture);
                done = true;
                break;

            case QueuedUpload::Kind::Buffer:
            {
                if (budget == 0)
                    break;
                const uint64_t size = std::min<uint64_t>(upload.data.size() - upload.progress, budget);
                mCommandList.UploadBuffer(upload.buffer, std::span(upload.data).subspan(upload.progress, size),
                                          upload.offset + upload.progress);
                upload.progress += size;
                budget -= size;
                done = upload.progress == upload.data.size();
                break;
            }

            case QueuedUpload::Kind::Texture:
            {
                if (budget == 0)
                    break;
                const Texture &texture = mTextures[upload.texture];
                const uint64_t row_bytes = MipRowBytes(texture, upload.mip);
                const uint32_t height = MipExtent(texture, upload.mip).height;
                const uint32_t rows_left = height - static_cast<uint32_t>(upload.progress);
                // At least one row per frame, even if a single row is bigger than the budget.
                const uint32_t rows = static_cast<uint32_t>(std::clamp<uint64_t>(budget / row_bytes, 1, rows_left));
                const uint32_t first_row = static_cast<uint32_t>(upload.progress);
                upload.progress += rows;
                done = upload.progress == height;
                // The texture is complete (for sampling) once its last queued operation is recorded.
                const bool last = done && texture.PendingUploads == 1;
                mCommandList.UploadTextureRows(upload.texture, upload.mip, first_row, rows,
                                               std::span(upload.data).subspan(first_row * row_bytes, rows * row_bytes),
                                               last);
                budget -= std::min(budget, rows * row_bytes);
                break;
            }
            }

            if (!done)
                break; // out of budget: continue next frame
            mUploadBurstBytes += upload.data.size();
            finish_queued(upload);
            mUploadQueue.pop_front();
        }

        mCommandList.EndMarker();
        ++mUploadBurstFrames;
        LMAO_PROFILE_PLOT("Queued upload bytes", static_cast<double>(mQueuedBytes));
        if (mUploadQueue.empty())
        {
            LMAO_LOG_INFO(LogChannel(), "Queued uploads done: {:.1f} MiB over {} frame(s)",
                          static_cast<double>(mUploadBurstBytes) / (1024.0 * 1024.0), mUploadBurstFrames);
            mUploadBurstBytes = 0;
            mUploadBurstFrames = 0;
        }
    }

} // namespace Lmao::Rhi::Vulkan
