module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module slotmap:free.freelist;

namespace inco {
    // a (not yet(?) intrusive) free-list
    //
    //
    // the free list has O(1) acquire but reuses slots in free order,
    // so if the slotmap experiences a lot of churn, the allocation pattern degrades toward random
    // which unlike the hierarchiccal bitmap results in poor cache locality
    //
    //also the list is not intrusive because it would mean the free finder abstraction would
    // need access into the storage which is a headache in and of itself
    // so yeah
    export class FreeList {
    public:
        static constexpr std::size_t nil = static_cast<std::size_t>(-1);
        static constexpr std::size_t npos = nil;

        [[nodiscard]] std::size_t acquire() noexcept {
            // if freelist is empty, caller needs to grow it
            if (head_ == nil) return npos;

            //get top free index
            const std::size_t i = head_;

            // pop top free index and feed it into head_
            head_ = next_[i];
            return i;
        }

        void release(std::size_t slot) noexcept {
            next_[slot] = head_;
            head_ = slot;
        }

        void grow(std::size_t slots) {
            next_.resize(slots);

            for (std::size_t i = slots; i-- > capacity_;) {
                next_[i] = head_;
                head_ = i;
            }
            capacity_ = slots;
        }

        [[nodiscard]] std::size_t capacity() const noexcept {
            return capacity_;
        }

    private:
        std::vector<std::size_t> next_{};
        std::size_t head_ = nil;
        std::size_t capacity_ = 0;
    };
}