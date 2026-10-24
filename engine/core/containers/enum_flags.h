#pragma once

// Bitwise operators for an `enum class` used as a set of flags:
//
//     enum class Usage : uint32_t { None = 0, Read = 1 << 0, Write = 1 << 1 };
//     LMAO_ENUM_FLAGS(Usage)
//     Usage u = Usage::Read | Usage::Write;
//     if (HasAny(u, Usage::Write)) ...
//
// Use at namespace scope, in the enum's namespace.

#include <type_traits>

#define LMAO_ENUM_FLAGS(Enum)                                                                                        \
    constexpr Enum operator|(Enum a, Enum b)                                                                         \
    {                                                                                                                \
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(a) | static_cast<std::underlying_type_t<Enum>>(b)); \
    }                                                                                                                \
    constexpr Enum operator&(Enum a, Enum b)                                                                         \
    {                                                                                                                \
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(a) & static_cast<std::underlying_type_t<Enum>>(b)); \
    }                                                                                                                \
    constexpr Enum &operator|=(Enum &a, Enum b) { return a = a | b; }                                                \
    constexpr bool HasAny(Enum flags, Enum mask) { return static_cast<std::underlying_type_t<Enum>>(flags & mask) != 0; }
