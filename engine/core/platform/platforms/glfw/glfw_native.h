#pragma once

// Kept apart from glfw_platform.cpp: glfw3native.h drags in <windows.h> / <X11/Xlib.h>, whose
// macros (None, Bool, Status, near, far, CreateWindow, ...) collide with engine names.

#include "core/platform/window.h"

struct GLFWwindow;

namespace Lmao::Glfw
{

    NativeWindowHandle GetNativeHandle(GLFWwindow *window);

} // namespace Lmao::Glfw
