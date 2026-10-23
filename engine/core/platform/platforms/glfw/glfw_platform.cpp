// GLFW implementation of Platform and Window. No GLFW type leaves this file.

#include "core/platform/platform.h"

#include "core/assert/assert.h"
#include "core/log/log.h"
#include "core/platform/platforms/glfw/glfw_native.h"
#include "core/profile/profile.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace Lmao
{
    namespace
    {

        spdlog::logger *log_channel()
        {
            static spdlog::logger *channel = Log::Channel("platform");
            return channel;
        }

        void on_glfw_error(int code, const char *description)
        {
            LMAO_LOG_ERROR(log_channel(), "GLFW error 0x{:X}: {}", code, description);
        }

        Key offset_key(Key first, int offset) { return static_cast<Key>(static_cast<int>(first) + offset); }

        Key to_key(int key)
        {
            if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
                return offset_key(Key::A, key - GLFW_KEY_A);
            if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
                return offset_key(Key::Num0, key - GLFW_KEY_0);
            if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F24)
                return offset_key(Key::F1, key - GLFW_KEY_F1);
            if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
                return offset_key(Key::Keypad0, key - GLFW_KEY_KP_0);

            switch (key)
            {
            case GLFW_KEY_SPACE: return Key::Space;
            case GLFW_KEY_APOSTROPHE: return Key::Apostrophe;
            case GLFW_KEY_COMMA: return Key::Comma;
            case GLFW_KEY_MINUS: return Key::Minus;
            case GLFW_KEY_PERIOD: return Key::Period;
            case GLFW_KEY_SLASH: return Key::Slash;
            case GLFW_KEY_SEMICOLON: return Key::Semicolon;
            case GLFW_KEY_EQUAL: return Key::Equal;
            case GLFW_KEY_LEFT_BRACKET: return Key::LeftBracket;
            case GLFW_KEY_BACKSLASH: return Key::Backslash;
            case GLFW_KEY_RIGHT_BRACKET: return Key::RightBracket;
            case GLFW_KEY_GRAVE_ACCENT: return Key::GraveAccent;
            case GLFW_KEY_ESCAPE: return Key::Escape;
            case GLFW_KEY_ENTER: return Key::Enter;
            case GLFW_KEY_TAB: return Key::Tab;
            case GLFW_KEY_BACKSPACE: return Key::Backspace;
            case GLFW_KEY_INSERT: return Key::Insert;
            case GLFW_KEY_DELETE: return Key::Delete;
            case GLFW_KEY_RIGHT: return Key::Right;
            case GLFW_KEY_LEFT: return Key::Left;
            case GLFW_KEY_DOWN: return Key::Down;
            case GLFW_KEY_UP: return Key::Up;
            case GLFW_KEY_PAGE_UP: return Key::PageUp;
            case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
            case GLFW_KEY_HOME: return Key::Home;
            case GLFW_KEY_END: return Key::End;
            case GLFW_KEY_CAPS_LOCK: return Key::CapsLock;
            case GLFW_KEY_SCROLL_LOCK: return Key::ScrollLock;
            case GLFW_KEY_NUM_LOCK: return Key::NumLock;
            case GLFW_KEY_PRINT_SCREEN: return Key::PrintScreen;
            case GLFW_KEY_PAUSE: return Key::Pause;
            case GLFW_KEY_MENU: return Key::Menu;
            case GLFW_KEY_KP_DECIMAL: return Key::KeypadDecimal;
            case GLFW_KEY_KP_DIVIDE: return Key::KeypadDivide;
            case GLFW_KEY_KP_MULTIPLY: return Key::KeypadMultiply;
            case GLFW_KEY_KP_SUBTRACT: return Key::KeypadSubtract;
            case GLFW_KEY_KP_ADD: return Key::KeypadAdd;
            case GLFW_KEY_KP_ENTER: return Key::KeypadEnter;
            case GLFW_KEY_KP_EQUAL: return Key::KeypadEqual;
            case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
            case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
            case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
            case GLFW_KEY_LEFT_SUPER: return Key::LeftSuper;
            case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
            case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
            case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
            case GLFW_KEY_RIGHT_SUPER: return Key::RightSuper;
            default: return Key::Unknown;
            }
        }

        KeyAction to_key_action(int action)
        {
            switch (action)
            {
            case GLFW_PRESS: return KeyAction::Press;
            case GLFW_REPEAT: return KeyAction::Repeat;
            default: return KeyAction::Release;
            }
        }

        Modifiers to_modifiers(int mods)
        {
            Modifiers result = Modifiers::None;
            if (mods & GLFW_MOD_SHIFT) result |= Modifiers::Shift;
            if (mods & GLFW_MOD_CONTROL) result |= Modifiers::Control;
            if (mods & GLFW_MOD_ALT) result |= Modifiers::Alt;
            if (mods & GLFW_MOD_SUPER) result |= Modifiers::Super;
            if (mods & GLFW_MOD_CAPS_LOCK) result |= Modifiers::CapsLock;
            if (mods & GLFW_MOD_NUM_LOCK) result |= Modifiers::NumLock;
            return result;
        }

        const char *to_platform_name(int platform)
        {
            switch (platform)
            {
            case GLFW_PLATFORM_WIN32: return "Win32";
            case GLFW_PLATFORM_COCOA: return "Cocoa";
            case GLFW_PLATFORM_WAYLAND: return "Wayland";
            case GLFW_PLATFORM_X11: return "X11";
            case GLFW_PLATFORM_NULL: return "Null";
            default: return "Unknown";
            }
        }

        std::atomic<bool> platform_exists{false};

        class GlfwPlatform final : public Platform
        {
        public:
            GlfwPlatform();
            ~GlfwPlatform() override;

            std::unique_ptr<Window> OpenWindow(const WindowDesc &desc) override;
            void PollEvents() override;
            void WaitEvents() override;
            std::span<const Event> GetEvents() const override { return mEvents; }
            std::string_view GetName() const override { return mName; }

            // Called from GLFW callbacks. Lands in the next PollEvents()/WaitEvents() batch.
            void PushEvent(Window *source, EventData data) { mPending.push_back({source, std::move(data)}); }
            void OnWindowDestroyed(Window *window);

        private:
            void swap_events();

            std::vector<Event> mEvents;  // returned by GetEvents()
            std::vector<Event> mPending; // filled by callbacks
            std::string mName;
            int mWindowCount = 0;
        };

        class GlfwWindow final : public Window
        {
        public:
            GlfwWindow(GlfwPlatform &platform, GLFWwindow *handle);
            ~GlfwWindow() override;

            UVec2 GetSize() const override;
            UVec2 GetFramebufferSize() const override;
            Vec2 GetContentScale() const override;
            bool IsMinimized() const override { return glfwGetWindowAttrib(mHandle, GLFW_ICONIFIED) != 0; }
            bool IsFocused() const override { return glfwGetWindowAttrib(mHandle, GLFW_FOCUSED) != 0; }
            bool ShouldClose() const override { return glfwWindowShouldClose(mHandle) != 0; }
            void SetShouldClose(bool should_close) override { glfwSetWindowShouldClose(mHandle, should_close ? GLFW_TRUE : GLFW_FALSE); }
            void SetTitle(std::string_view title) override { glfwSetWindowTitle(mHandle, std::string(title).c_str()); }
            NativeWindowHandle GetNativeHandle() const override { return Glfw::GetNativeHandle(mHandle); }

            GlfwPlatform &GetPlatform() { return mPlatform; }

        private:
            void install_callbacks();

            GlfwPlatform &mPlatform;
            GLFWwindow *mHandle;
        };

        // --- GLFW callbacks: translate and queue ----------------------------------------------

        void push_event(GLFWwindow *handle, EventData data)
        {
            auto *window = static_cast<GlfwWindow *>(glfwGetWindowUserPointer(handle));
            window->GetPlatform().PushEvent(window, std::move(data));
        }

        void on_close(GLFWwindow *handle) { push_event(handle, WindowCloseEvent{}); }

        void on_size(GLFWwindow *handle, int width, int height)
        {
            push_event(handle, WindowResizeEvent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
        }

        void on_framebuffer_size(GLFWwindow *handle, int width, int height)
        {
            push_event(handle, FramebufferResizeEvent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
        }

        void on_focus(GLFWwindow *handle, int focused) { push_event(handle, WindowFocusEvent{focused != 0}); }

        void on_iconify(GLFWwindow *handle, int iconified) { push_event(handle, WindowMinimizeEvent{iconified != 0}); }

        void on_key(GLFWwindow *handle, int key, int scancode, int action, int mods)
        {
            push_event(handle, KeyEvent{to_key(key), to_key_action(action), to_modifiers(mods), scancode});
        }

        void on_char(GLFWwindow *handle, unsigned int codepoint)
        {
            push_event(handle, TextEvent{static_cast<char32_t>(codepoint)});
        }

        void on_mouse_button(GLFWwindow *handle, int button, int action, int mods)
        {
            if (button < 0 || button >= static_cast<int>(MouseButton::Count))
                return; // GLFW has 8 buttons; we expose 5
            push_event(handle, MouseButtonEvent{static_cast<MouseButton>(button), action == GLFW_PRESS, to_modifiers(mods)});
        }

        void on_cursor_pos(GLFWwindow *handle, double x, double y)
        {
            push_event(handle, MouseMoveEvent{Vec2(static_cast<float>(x), static_cast<float>(y))});
        }

        void on_scroll(GLFWwindow *handle, double x, double y)
        {
            push_event(handle, MouseScrollEvent{Vec2(static_cast<float>(x), static_cast<float>(y))});
        }

        void on_cursor_enter(GLFWwindow *handle, int entered) { push_event(handle, CursorEnterEvent{entered != 0}); }

        // --- GlfwPlatform ---------------------------------------------------------------------

        GlfwPlatform::GlfwPlatform()
        {
            int major = 0, minor = 0, revision = 0;
            glfwGetVersion(&major, &minor, &revision);
            mName = fmt::format("GLFW {}.{}.{} ({})", major, minor, revision, to_platform_name(glfwGetPlatform()));
            LMAO_LOG_INFO(log_channel(), "Platform: {}", mName);
        }

        GlfwPlatform::~GlfwPlatform()
        {
            LMAO_ASSERT(mWindowCount == 0, "{} window(s) still open when the platform was destroyed", mWindowCount);
            glfwTerminate();
            platform_exists = false;
        }

        std::unique_ptr<Window> GlfwPlatform::OpenWindow(const WindowDesc &desc)
        {
            LMAO_PROFILE_ZONE();
            glfwDefaultWindowHints();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // the RHI creates the surface
            glfwWindowHint(GLFW_RESIZABLE, desc.Resizable ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_VISIBLE, desc.Visible ? GLFW_TRUE : GLFW_FALSE);

            const std::string title(desc.Title);
            GLFWwindow *handle = glfwCreateWindow(static_cast<int>(desc.Width), static_cast<int>(desc.Height),
                                                  title.c_str(), nullptr, nullptr);
            if (!handle)
            {
                LMAO_LOG_ERROR(log_channel(), "Failed to create window '{}' ({}x{})", desc.Title, desc.Width, desc.Height);
                return nullptr;
            }

            ++mWindowCount;
            LMAO_LOG_INFO(log_channel(), "Opened window '{}' ({}x{})", desc.Title, desc.Width, desc.Height);
            return std::make_unique<GlfwWindow>(*this, handle);
        }

        void GlfwPlatform::PollEvents()
        {
            LMAO_PROFILE_ZONE();
            glfwPollEvents();
            swap_events();
        }

        void GlfwPlatform::WaitEvents()
        {
            LMAO_PROFILE_ZONE();
            glfwWaitEvents();
            swap_events();
        }

        void GlfwPlatform::swap_events()
        {
            mEvents.swap(mPending);
            mPending.clear();
        }

        void GlfwPlatform::OnWindowDestroyed(Window *window)
        {
            --mWindowCount;
            // Drop queued events that would point at the destroyed window.
            auto from_window = [window](const Event &event) { return event.Source == window; };
            std::erase_if(mEvents, from_window);
            std::erase_if(mPending, from_window);
        }

        // --- GlfwWindow -----------------------------------------------------------------------

        GlfwWindow::GlfwWindow(GlfwPlatform &platform, GLFWwindow *handle) : mPlatform(platform), mHandle(handle)
        {
            glfwSetWindowUserPointer(mHandle, this);
            install_callbacks();
        }

        GlfwWindow::~GlfwWindow()
        {
            glfwDestroyWindow(mHandle);
            mPlatform.OnWindowDestroyed(this);
        }

        void GlfwWindow::install_callbacks()
        {
            glfwSetWindowCloseCallback(mHandle, &on_close);
            glfwSetWindowSizeCallback(mHandle, &on_size);
            glfwSetFramebufferSizeCallback(mHandle, &on_framebuffer_size);
            glfwSetWindowFocusCallback(mHandle, &on_focus);
            glfwSetWindowIconifyCallback(mHandle, &on_iconify);
            glfwSetKeyCallback(mHandle, &on_key);
            glfwSetCharCallback(mHandle, &on_char);
            glfwSetMouseButtonCallback(mHandle, &on_mouse_button);
            glfwSetCursorPosCallback(mHandle, &on_cursor_pos);
            glfwSetScrollCallback(mHandle, &on_scroll);
            glfwSetCursorEnterCallback(mHandle, &on_cursor_enter);
        }

        UVec2 GlfwWindow::GetSize() const
        {
            int width = 0, height = 0;
            glfwGetWindowSize(mHandle, &width, &height);
            return UVec2(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }

        UVec2 GlfwWindow::GetFramebufferSize() const
        {
            int width = 0, height = 0;
            glfwGetFramebufferSize(mHandle, &width, &height);
            return UVec2(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }

        Vec2 GlfwWindow::GetContentScale() const
        {
            float x = 1.0f, y = 1.0f;
            glfwGetWindowContentScale(mHandle, &x, &y);
            return Vec2(x, y);
        }

    } // namespace

    std::unique_ptr<Platform> Platform::Create(const PlatformDesc &desc)
    {
        LMAO_PROFILE_ZONE();
        LMAO_VERIFY(!platform_exists.exchange(true), "only one Platform may exist at a time");

        glfwSetErrorCallback(&on_glfw_error);
#if defined(__linux__)
        if (desc.Display == DisplayBackend::X11)
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        else if (desc.Display == DisplayBackend::Wayland)
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        else
            glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
#else
        (void)desc;
#endif
        if (!glfwInit())
        {
            LMAO_LOG_ERROR(log_channel(), "glfwInit failed");
            platform_exists = false;
            return nullptr;
        }
        return std::make_unique<GlfwPlatform>();
    }

} // namespace Lmao
