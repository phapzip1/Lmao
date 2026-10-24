// Device::Create: the only place that knows which backends are built in.

#include "rhi/device.h"

#include "core/log/log.h"
#include "core/profile/profile.h"
#include "rhi/rhis/vulkan/vulkan_device.h"

namespace Lmao::Rhi
{

    std::unique_ptr<Device> Device::Create(const DeviceDesc &desc)
    {
        LMAO_PROFILE_ZONE();
        switch (desc.Api)
        {
        case Backend::Vulkan:
            return Vulkan::CreateDevice(desc);
        }
        LMAO_ERROR("Unknown RHI backend {}", static_cast<int>(desc.Api));
        return nullptr;
    }

} // namespace Lmao::Rhi
