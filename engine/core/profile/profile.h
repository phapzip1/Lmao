#pragma once

// Profiling markers, backed by Tracy. They compile to nothing unless profiling is enabled
// (LMAO_ENABLE_PROFILING, which sets TRACY_ENABLE). Data is only collected while the Tracy
// profiler is connected (TRACY_ON_DEMAND).
//
// Names must be string literals (or otherwise outlive the program).

#include <tracy/Tracy.hpp>

#define LMAO_PROFILE_ZONE() ZoneScoped // zone named after the enclosing function
#define LMAO_PROFILE_ZONE_NAMED(name) ZoneScopedN(name)
#define LMAO_PROFILE_ZONE_COLOR(name, rgb) ZoneScopedNC(name, rgb) // rgb: 0xRRGGBB
#define LMAO_PROFILE_ZONE_TEXT(text, size) ZoneText(text, size)    // attach runtime text to the current zone

#define LMAO_PROFILE_FRAME() FrameMark // call once at the end of every frame
#define LMAO_PROFILE_FRAME_NAMED(name) FrameMarkNamed(name)

#define LMAO_PROFILE_PLOT(name, value) TracyPlot(name, value)
#define LMAO_PROFILE_MESSAGE(literal) TracyMessageL(literal)

#define LMAO_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define LMAO_PROFILE_FREE(ptr) TracyFree(ptr)

#if defined(TRACY_ENABLE)
#define LMAO_PROFILE_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define LMAO_PROFILE_THREAD_NAME(name) ((void)0)
#endif
