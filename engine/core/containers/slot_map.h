#pragma once

// SlotMap<T, Tag>: stores values contiguously and hands out generational handles to them.
//
// - Insert / Remove / Get are O(1). Removing swaps the last value into the hole, so values stay
//   densely packed and iteration (begin/end, Values()) is a plain array walk.
// - Value addresses are NOT stable across Insert/Remove; keep the handle, not a pointer.
// - Freed slots are reused through an intrusive free list; their generation is bumped so old
//   handles fail lookup.
//
// Uses std::vector for now; allocator support comes with the M3 memory work.

#include "core/assert/assert.h"
#include "core/containers/handle.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Lmao
{

    template <typename T, typename Tag = T>
    class SlotMap
    {
    public:
        using HandleType = Handle<Tag>;

        template <typename... Args>
        HandleType Emplace(Args &&...args)
        {
            const uint32_t slot_index = acquire_slot();
            Slot &slot = mSlots[slot_index];
            slot.dense_index = static_cast<uint32_t>(mValues.size());
            mValues.emplace_back(std::forward<Args>(args)...);
            mDenseToSlot.push_back(slot_index);
            return {slot_index, slot.generation};
        }

        HandleType Insert(const T &value) { return Emplace(value); }
        HandleType Insert(T &&value) { return Emplace(std::move(value)); }

        // Returns false if the handle was already stale.
        bool Remove(HandleType handle)
        {
            if (!Contains(handle))
                return false;

            Slot &slot = mSlots[handle.Index];
            const uint32_t hole = slot.dense_index;
            const uint32_t last = static_cast<uint32_t>(mValues.size() - 1);
            if (hole != last)
            {
                mValues[hole] = std::move(mValues[last]);
                mDenseToSlot[hole] = mDenseToSlot[last];
                mSlots[mDenseToSlot[hole]].dense_index = hole;
            }
            mValues.pop_back();
            mDenseToSlot.pop_back();

            release_slot(handle.Index);
            return true;
        }

        bool Contains(HandleType handle) const
        {
            return handle.Index < mSlots.size() && handle.Generation != 0 &&
                   mSlots[handle.Index].generation == handle.Generation;
        }

        // nullptr if the handle is stale.
        T *Get(HandleType handle) { return Contains(handle) ? &mValues[mSlots[handle.Index].dense_index] : nullptr; }
        const T *Get(HandleType handle) const { return Contains(handle) ? &mValues[mSlots[handle.Index].dense_index] : nullptr; }

        // Asserts that the handle is live.
        T &operator[](HandleType handle)
        {
            LMAO_ASSERT(Contains(handle), "stale or invalid handle (index {}, generation {})", handle.Index, handle.Generation);
            return mValues[mSlots[handle.Index].dense_index];
        }
        const T &operator[](HandleType handle) const
        {
            LMAO_ASSERT(Contains(handle), "stale or invalid handle (index {}, generation {})", handle.Index, handle.Generation);
            return mValues[mSlots[handle.Index].dense_index];
        }

        // The handle of the value at a dense position, e.g. while iterating Values().
        HandleType HandleAt(size_t dense_index) const
        {
            LMAO_ASSERT(dense_index < mValues.size());
            const uint32_t slot_index = mDenseToSlot[dense_index];
            return {slot_index, mSlots[slot_index].generation};
        }

        void Clear()
        {
            for (uint32_t slot_index : mDenseToSlot)
                release_slot(slot_index);
            mValues.clear();
            mDenseToSlot.clear();
        }

        void Reserve(size_t capacity)
        {
            mValues.reserve(capacity);
            mDenseToSlot.reserve(capacity);
            mSlots.reserve(capacity);
        }

        size_t Size() const { return mValues.size(); }
        bool IsEmpty() const { return mValues.empty(); }

        std::span<T> Values() { return mValues; }
        std::span<const T> Values() const { return mValues; }

        auto begin() { return mValues.begin(); }
        auto end() { return mValues.end(); }
        auto begin() const { return mValues.begin(); }
        auto end() const { return mValues.end(); }

    private:
        static constexpr uint32_t end_of_free_list = UINT32_MAX;

        struct Slot
        {
            uint32_t dense_index; // index into mValues while live; next free slot while free
            uint32_t generation;  // never 0
        };

        uint32_t acquire_slot()
        {
            if (mFreeHead != end_of_free_list)
            {
                const uint32_t slot_index = mFreeHead;
                mFreeHead = mSlots[slot_index].dense_index;
                return slot_index;
            }
            LMAO_ASSERT(mSlots.size() < Handle<Tag>::InvalidIndex, "SlotMap is full");
            mSlots.push_back({0, 1});
            return static_cast<uint32_t>(mSlots.size() - 1);
        }

        void release_slot(uint32_t slot_index)
        {
            Slot &slot = mSlots[slot_index];
            if (++slot.generation == 0) // skip 0 on wrap-around: it means "invalid"
                slot.generation = 1;
            slot.dense_index = mFreeHead;
            mFreeHead = slot_index;
        }

        std::vector<T> mValues;
        std::vector<uint32_t> mDenseToSlot;
        std::vector<Slot> mSlots;
        uint32_t mFreeHead = end_of_free_list;
    };

} // namespace Lmao
