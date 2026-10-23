#include "core/platform/input.h"

namespace Lmao
{

    const char *ToString(Key key)
    {
        switch (key)
        {
#define LMAO_KEY_CASE_(name) \
    case Key::name:          \
        return #name;
            LMAO_KEY_LIST(LMAO_KEY_CASE_)
#undef LMAO_KEY_CASE_
        case Key::Count:
            break;
        }
        return "Invalid";
    }

    const char *ToString(KeyAction action)
    {
        switch (action)
        {
        case KeyAction::Press:
            return "Press";
        case KeyAction::Release:
            return "Release";
        case KeyAction::Repeat:
            return "Repeat";
        }
        return "Invalid";
    }

    const char *ToString(MouseButton button)
    {
        switch (button)
        {
        case MouseButton::Left:
            return "Left";
        case MouseButton::Right:
            return "Right";
        case MouseButton::Middle:
            return "Middle";
        case MouseButton::X1:
            return "X1";
        case MouseButton::X2:
            return "X2";
        case MouseButton::Count:
            break;
        }
        return "Invalid";
    }

} // namespace Lmao
