#pragma once

// Generational handle: an index into a SlotMap plus the generation of the slot when the
// handle was issued. A handle whose slot has since been freed (and maybe reused) no longer
// matches the slot's generation, so stale handles are detected instead of aliasing new data.
//
// Tag makes handles to different things distinct types: Handle<struct TextureTag>.

#include <cstdint>
#include <functional>

namespace Lmao
{

    template <typename Tag>
    struct Handle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0; // 0 is never issued, so a default handle is always invalid

        constexpr bool IsValid() const { return Generation != 0; }
        constexpr explicit operator bool() const { return IsValid(); }

        friend constexpr bool operator==(Handle, Handle) = default;
    };

} // namespace Lmao

template <typename Tag>
struct std::hash<Lmao::Handle<Tag>>
{
    size_t operator()(Lmao::Handle<Tag> handle) const noexcept
    {
        return std::hash<uint64_t>{}((uint64_t(handle.Generation) << 32) | handle.Index);
    }
};
