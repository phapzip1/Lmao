#pragma once

// Engine math on top of glm. Conventions: right-handed, Y-up, -Z forward, column-major,
// depth 0..1 with reverse-Z (near = 1, far = 0; clear 0, compare GREATER).
// GLM_FORCE_DEPTH_ZERO_TO_ONE and GLM_FORCE_RIGHT_HANDED are set on the glm target.
//
// Vec3 / Mat3 are for CPU code only; never put them in GPU buffers (12-byte stride).

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Lmao
{

    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;
    using IVec2 = glm::ivec2;
    using IVec3 = glm::ivec3;
    using IVec4 = glm::ivec4;
    using UVec2 = glm::uvec2;
    using UVec3 = glm::uvec3;
    using UVec4 = glm::uvec4;
    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;
    using Quat = glm::quat; // stored x, y, z, w; but the constructor is Quat(w, x, y, z)

    static_assert(sizeof(Vec2) == 8);
    static_assert(sizeof(Vec3) == 12);
    static_assert(sizeof(Vec4) == 16);
    static_assert(sizeof(Mat4) == 64);
    static_assert(sizeof(Quat) == 16);

    namespace Math
    {

        inline constexpr float Pi = glm::pi<float>();
        inline constexpr float TwoPi = glm::two_pi<float>();
        inline constexpr float HalfPi = glm::half_pi<float>();

        inline constexpr Vec3 Right{1.0f, 0.0f, 0.0f};
        inline constexpr Vec3 Up{0.0f, 1.0f, 0.0f};
        inline constexpr Vec3 Forward{0.0f, 0.0f, -1.0f};

        constexpr float Radians(float degrees) { return degrees * (Pi / 180.0f); }
        constexpr float Degrees(float radians) { return radians * (180.0f / Pi); }

        // Reverse-Z perspective: view-space depth `z_near` maps to 1, `z_far` to 0.
        // No Y flip; Vulkan flips with a negative viewport height.
        inline Mat4 PerspectiveReverseZ(float fovy_radians, float aspect, float z_near, float z_far)
        {
            return glm::perspectiveRH_ZO(fovy_radians, aspect, z_far, z_near); // swapping near/far reverses Z
        }

        // Reverse-Z perspective with the far plane at infinity: depth = z_near / distance.
        inline Mat4 PerspectiveInfiniteReverseZ(float fovy_radians, float aspect, float z_near)
        {
            const float f = 1.0f / glm::tan(fovy_radians * 0.5f);
            Mat4 m(0.0f);
            m[0][0] = f / aspect;
            m[1][1] = f;
            m[2][3] = -1.0f; // w = -z_view
            m[3][2] = z_near;
            return m;
        }

        inline Mat4 LookAt(const Vec3 &eye, const Vec3 &target, const Vec3 &up = Up)
        {
            return glm::lookAtRH(eye, target, up);
        }

    } // namespace Math
} // namespace Lmao
