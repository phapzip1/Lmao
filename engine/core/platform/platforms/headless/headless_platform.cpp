#include "core/platform/platforms/headless/headless_platform.h"

#include "core/log/log.h"

#include <string>
#include <vector>

namespace Lmao
{
    namespace
    {

        class HeadlessWindow final : public Window
        {
        public:
            explicit HeadlessWindow(const WindowDesc &desc)
                : mSize(desc.Width, desc.Height), mPosition(desc.HasPosition ? desc.Position : IVec2(0, 0))
            {
            }

            UVec2 GetSize() const override { return mSize; }
            UVec2 GetFramebufferSize() const override { return mSize; } // content scale 1
            Vec2 GetContentScale() const override { return Vec2(1.0f); }

            bool IsMinimized() const override { return false; }
            bool IsFocused() const override { return true; }

            bool ShouldClose() const override { return mShouldClose; }
            void SetShouldClose(bool should_close) override { mShouldClose = should_close; }

            void SetTitle(std::string_view) override {}

            IVec2 GetPosition() const override { return mPosition; }
            void SetPosition(IVec2 position) override { mPosition = position; }
            void SetSize(UVec2 size) override { mSize = size; }

            void Show() override {}
            void Focus() override {}
            void SetOpacity(float) override {}
            void SetMousePassthrough(bool) override {}
            void SetCursor(StandardCursor) override {}
            Vec2 GetCursorPosition() const override { return Vec2(0.0f); }
            void SetCursorMode(CursorMode mode) override { mCursorMode = mode; }
            CursorMode GetCursorMode() const override { return mCursorMode; }

            NativeWindowHandle GetNativeHandle() const override { return {}; } // NativeWindowType::None

        private:
            UVec2 mSize;
            IVec2 mPosition;
            bool mShouldClose = false;
            CursorMode mCursorMode = CursorMode::Normal;
        };

        class HeadlessPlatform final : public Platform
        {
        public:
            std::unique_ptr<Window> OpenWindow(const WindowDesc &desc) override
            {
                return std::make_unique<HeadlessWindow>(desc);
            }

            void PollEvents() override {}
            void WaitEvents() override {} // nothing will ever arrive: don't block
            std::span<const Event> GetEvents() const override { return {}; }

            std::string_view GetName() const override { return "Headless"; }

            bool SupportsWindowPositioning() const override { return false; }
            std::vector<MonitorInfo> GetMonitors() const override
            {
                // One virtual 1080p monitor, so code that lays things out per monitor still works.
                return {{.Size = {1920, 1080}, .WorkSize = {1920, 1080}}};
            }

            void SetClipboardText(std::string_view text) override { mClipboard = text; }
            std::string GetClipboardText() const override { return mClipboard; }

        private:
            std::string mClipboard;
        };

    } // namespace

    std::unique_ptr<Platform> CreateHeadlessPlatform()
    {
        LMAO_LOG_INFO(Log::Channel("core"), "Platform: headless (no display, no input)");
        return std::make_unique<HeadlessPlatform>();
    }

} // namespace Lmao
