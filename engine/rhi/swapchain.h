#pragma once

// Presentation to a window. Created by Device::CreateSwapchain and driven by
// Device::BeginFrame / EndFrame. Destroy it before the Device and before its Window.

#include "core/math/math.h"
#include "core/platform/window.h"
#include "rhi/types.h"

#include <cstdint>

namespace Lmao::Rhi
{

    struct SwapchainDesc
    {
        NativeWindowHandle Window;
        uint32_t Width = 0; // framebuffer size in pixels (Window::GetFramebufferSize)
        uint32_t Height = 0;
        bool VSync = true; // false: lowest latency the OS allows (mailbox, else immediate)
    };

    class Swapchain
    {
    public:
        virtual ~Swapchain() = default;

        Swapchain(const Swapchain &) = delete;
        Swapchain &operator=(const Swapchain &) = delete;

        // Call on FramebufferResizeEvent. The swapchain is recreated at the next BeginFrame;
        // while the size is 0 x 0 (minimized) BeginFrame skips frames.
        virtual void Resize(uint32_t width, uint32_t height) = 0;
        virtual void SetVSync(bool vsync) = 0;

        virtual UVec2 GetSize() const = 0;
        virtual Format GetFormat() const = 0;
        virtual uint32_t GetImageCount() const = 0;

        // The image being rendered this frame. Only valid between BeginFrame and EndFrame;
        // the handle changes when the swapchain is recreated.
        virtual TextureHandle GetCurrentTexture() const = 0;

    protected:
        Swapchain() = default;
    };

} // namespace Lmao::Rhi
