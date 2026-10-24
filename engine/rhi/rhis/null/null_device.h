#pragma once

// Null backend entry point, called by Rhi::Device::Create for Backend::Null. Talks to no GPU:
// every call succeeds and returns valid handles, command lists record nothing, and swapchains
// need no window (NativeWindowType::None). For headless runs (CI smoke tests, servers, tools).
// It keeps the Vulkan backend's handle and usage checks, so API misuse still asserts headless.

#include "rhi/device.h"

#include <memory>

namespace Lmao::Rhi::Null
{

    std::unique_ptr<Device> CreateDevice(const DeviceDesc &desc);

} // namespace Lmao::Rhi::Null
