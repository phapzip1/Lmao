#pragma once

// Sandbox fly camera: hold the right mouse button to look around (the cursor is captured),
// WASD to move, Q / E down / up, Shift to go faster, mouse wheel to change speed.

#include "core/math/math.h"
#include "core/platform/event.h"
#include "core/platform/window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <variant>

namespace Sandbox
{
    using namespace Lmao;

    class FlyCamera
    {
    public:
        // Looking at `target` from `position`.
        void LookAt(const Vec3 &position, const Vec3 &target)
        {
            mPosition = position;
            const Vec3 forward = glm::normalize(target - position);
            mPitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
            mYaw = std::atan2(forward.x, -forward.z);
        }

        void SetSpeed(float units_per_second) { mSpeed = units_per_second; }
        float GetSpeed() const { return mSpeed; }

        void OnEvent(const Event &event, Window &window)
        {
            if (const auto *key = std::get_if<KeyEvent>(&event.Data))
            {
                if (key->Code != Key::Unknown)
                    mKeys[static_cast<size_t>(key->Code)] = key->Action != KeyAction::Release;
            }
            else if (const auto *button = std::get_if<MouseButtonEvent>(&event.Data))
            {
                if (button->Button == MouseButton::Right)
                {
                    window.SetCursorMode(button->Pressed ? CursorMode::Captured : CursorMode::Normal);
                    mHasLastCursor = false; // the cursor position jumps when capture starts or ends
                }
            }
            else if (const auto *move = std::get_if<MouseMoveEvent>(&event.Data))
            {
                if (window.GetCursorMode() == CursorMode::Captured && mHasLastCursor)
                {
                    const Vec2 delta = move->Position - mLastCursor;
                    mYaw += delta.x * look_sensitivity;
                    mPitch = std::clamp(mPitch - delta.y * look_sensitivity, -1.55f, 1.55f);
                }
                mLastCursor = move->Position;
                mHasLastCursor = true;
            }
            else if (const auto *scroll = std::get_if<MouseScrollEvent>(&event.Data))
            {
                mSpeed = std::max(mSpeed * std::pow(1.2f, scroll->Offset.y), 0.01f);
            }
            else if (std::holds_alternative<WindowFocusEvent>(event.Data))
            {
                mKeys.fill(false); // releases that happen while unfocused are never reported
            }
        }

        void Update(float delta_time)
        {
            const Vec3 forward = GetForward();
            const Vec3 right = glm::normalize(glm::cross(forward, Math::Up));
            Vec3 move(0.0f);
            if (held(Key::W)) move += forward;
            if (held(Key::S)) move -= forward;
            if (held(Key::D)) move += right;
            if (held(Key::A)) move -= right;
            if (held(Key::E)) move += Math::Up;
            if (held(Key::Q)) move -= Math::Up;
            if (glm::length(move) > 0.0f)
            {
                const float boost = held(Key::LeftShift) || held(Key::RightShift) ? 4.0f : 1.0f;
                mPosition += glm::normalize(move) * mSpeed * boost * delta_time;
            }
        }

        // Yaw 0 looks down -Z (engine forward); positive yaw turns right, positive pitch up.
        Vec3 GetForward() const
        {
            return {std::sin(mYaw) * std::cos(mPitch), std::sin(mPitch), -std::cos(mYaw) * std::cos(mPitch)};
        }
        const Vec3 &GetPosition() const { return mPosition; }
        Mat4 GetView() const { return Math::LookAt(mPosition, mPosition + GetForward()); }

    private:
        static constexpr float look_sensitivity = 0.0025f; // radians per pixel

        bool held(Key key) const { return mKeys[static_cast<size_t>(key)]; }

        Vec3 mPosition{0.0f, 0.0f, 3.0f};
        float mYaw = 0.0f;
        float mPitch = 0.0f;
        float mSpeed = 2.0f;
        std::array<bool, static_cast<size_t>(Key::Count)> mKeys{};
        Vec2 mLastCursor{0.0f};
        bool mHasLastCursor = false;
    };

} // namespace Sandbox
