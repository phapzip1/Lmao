#pragma once

// Platform with no display (DisplayBackend::Headless), for CI and tools: windows are plain
// objects with a fixed size and a NativeWindowType::None handle, there are no input events and
// the clipboard is process-local. Pairs with the null RHI backend, whose swapchains need no
// real window.

#include "core/platform/platform.h"

#include <memory>

namespace Lmao
{

    std::unique_ptr<Platform> CreateHeadlessPlatform();

} // namespace Lmao
