#pragma once

// Backend-independent input codes. Keys name the physical key on a US layout; use TextEvent
// for typed characters.

#include <cstdint>

// ENTRY(Name) for every Key, in order. Ranges A..Z, Num0..Num9, F1..F24 and Keypad0..Keypad9 are
// contiguous so backends can map them with an offset.
#define LMAO_KEY_LIST(ENTRY)                                                                                     \
    ENTRY(Unknown)                                                                                              \
    ENTRY(Space) ENTRY(Apostrophe) ENTRY(Comma) ENTRY(Minus) ENTRY(Period) ENTRY(Slash) ENTRY(Semicolon) ENTRY(Equal)                       \
    ENTRY(LeftBracket) ENTRY(Backslash) ENTRY(RightBracket) ENTRY(GraveAccent)                                              \
    ENTRY(Num0) ENTRY(Num1) ENTRY(Num2) ENTRY(Num3) ENTRY(Num4) ENTRY(Num5) ENTRY(Num6) ENTRY(Num7) ENTRY(Num8) ENTRY(Num9)                         \
    ENTRY(A) ENTRY(B) ENTRY(C) ENTRY(D) ENTRY(E) ENTRY(F) ENTRY(G) ENTRY(H) ENTRY(I) ENTRY(J) ENTRY(K) ENTRY(L) ENTRY(M)                                        \
    ENTRY(N) ENTRY(O) ENTRY(P) ENTRY(Q) ENTRY(R) ENTRY(S) ENTRY(T) ENTRY(U) ENTRY(V) ENTRY(W) ENTRY(X) ENTRY(Y) ENTRY(Z)                                        \
    ENTRY(Escape) ENTRY(Enter) ENTRY(Tab) ENTRY(Backspace) ENTRY(Insert) ENTRY(Delete)                                              \
    ENTRY(Right) ENTRY(Left) ENTRY(Down) ENTRY(Up) ENTRY(PageUp) ENTRY(PageDown) ENTRY(Home) ENTRY(End)                                     \
    ENTRY(CapsLock) ENTRY(ScrollLock) ENTRY(NumLock) ENTRY(PrintScreen) ENTRY(Pause) ENTRY(Menu)                                    \
    ENTRY(F1) ENTRY(F2) ENTRY(F3) ENTRY(F4) ENTRY(F5) ENTRY(F6) ENTRY(F7) ENTRY(F8) ENTRY(F9) ENTRY(F10) ENTRY(F11) ENTRY(F12)                              \
    ENTRY(F13) ENTRY(F14) ENTRY(F15) ENTRY(F16) ENTRY(F17) ENTRY(F18) ENTRY(F19) ENTRY(F20) ENTRY(F21) ENTRY(F22) ENTRY(F23) ENTRY(F24)                     \
    ENTRY(Keypad0) ENTRY(Keypad1) ENTRY(Keypad2) ENTRY(Keypad3) ENTRY(Keypad4) ENTRY(Keypad5) ENTRY(Keypad6) ENTRY(Keypad7) ENTRY(Keypad8)      \
    ENTRY(Keypad9) ENTRY(KeypadDecimal) ENTRY(KeypadDivide) ENTRY(KeypadMultiply) ENTRY(KeypadSubtract) ENTRY(KeypadAdd)            \
    ENTRY(KeypadEnter) ENTRY(KeypadEqual)                                                                           \
    ENTRY(LeftShift) ENTRY(LeftControl) ENTRY(LeftAlt) ENTRY(LeftSuper) ENTRY(RightShift) ENTRY(RightControl) ENTRY(RightAlt) ENTRY(RightSuper)

namespace Lmao
{

    enum class Key : uint16_t
    {
#define LMAO_KEY_ENUM_(name) name,
        LMAO_KEY_LIST(LMAO_KEY_ENUM_)
#undef LMAO_KEY_ENUM_
            Count
    };

    enum class KeyAction : uint8_t
    {
        Press,
        Release,
        Repeat
    };

    enum class MouseButton : uint8_t
    {
        Left,
        Right,
        Middle,
        X1,
        X2,
        Count
    };

    enum class Modifiers : uint8_t
    {
        None = 0,
        Shift = 1 << 0,
        Control = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3,
        CapsLock = 1 << 4,
        NumLock = 1 << 5,
    };

    constexpr Modifiers operator|(Modifiers a, Modifiers b) { return Modifiers(uint8_t(a) | uint8_t(b)); }
    constexpr Modifiers operator&(Modifiers a, Modifiers b) { return Modifiers(uint8_t(a) & uint8_t(b)); }
    constexpr Modifiers &operator|=(Modifiers &a, Modifiers b) { return a = a | b; }
    constexpr bool HasAny(Modifiers mods, Modifiers mask) { return (mods & mask) != Modifiers::None; }

    const char *ToString(Key key);
    const char *ToString(KeyAction action);
    const char *ToString(MouseButton button);

} // namespace Lmao
