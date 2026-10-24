#pragma once

// Vulkan backend entry point, called by Rhi::Device::Create. Includes no Vulkan headers:
// only rhi/ types cross this boundary.

#include "rhi/device.h"

#include <memory>

namespace Lmao::Rhi::Vulkan
{

    std::unique_ptr<Device> CreateDevice(const DeviceDesc &desc);

} // namespace Lmao::Rhi::Vulkan
