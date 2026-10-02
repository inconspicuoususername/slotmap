module;

#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>
// #include "absl/container/flat_hash_set.h"

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
        using index_type = std::size_t;
        static constexpr index_type nil = static_cast<index_type>(-1);
        static constexpr index_type npos = nil;

        FreeList() = default;
        FreeList(const FreeList&) = default;
        FreeList& operator=(const FreeList&) = default;

        FreeList(FreeList&& o) noexcept
            : _free_set(std::move(o._free_set)),
              _high_water_mark(std::exchange(o._high_water_mark, nil)),
              _capacity(std::exchange(o._capacity, 0)) {}

        FreeList& operator=(FreeList&& o) noexcept {
            if (this != &o) {
                _free_set = std::move(o._free_set);
                _high_water_mark = std::exchange(o._high_water_mark, nil);
                _capacity = std::exchange(o._capacity, 0);
            }
            return *this;
        }

        [[nodiscard]] index_type acquire() noexcept {
            // if freelist is empty, caller needs to grow it
            if (_free_set.empty()) return npos;

            //get top free index
            // const index_type i = _high_water_mark;

            // pop top free index and feed it into head_
            // head_ = next_[i];
            const auto it = _free_set.begin();
            if (it == _free_set.end()) return npos;
            const index_type ret = *it;
            _free_set.erase(it);
            return ret;
        }

        void release(index_type slot) noexcept {
            _free_set.emplace(slot);
        }

        void grow(std::size_t slots) {
            // next_.resize(slots);
            // no way to resize the std priority queue.... thank you wg 21
            if (_capacity >= slots) return;
            _high_water_mark = slots;
            for (index_type i = slots; i-- > _capacity;) _free_set.emplace(i);
            _capacity = slots;
        }

        [[nodiscard]] index_type capacity() const noexcept {
            return _capacity;
        }

        // [[nodiscard]] std::vector<bool> live_slots() const {
        //     std::vector<bool> live(capacity_, true);
        //     for (index_type i = head_; i != nil; i = next_[i])
        //         live[i] = false;
        //     return live;
        // }

        [[nodiscard]] bool is_live(const index_type slot) const noexcept {
            return slot < _capacity && !_free_set.contains(slot);
        }

    private:
        // std::vector<index_type> next_{};
        // use deque because the stupid std implementation has no resize() method
        // std::priority_queue<index_type, std::deque<index_type>, std::greater<>> _free_list;
        std::unordered_set<index_type> _free_set;
        // std::size_t head_ = nil;
        std::size_t _high_water_mark = 0;
        std::size_t _capacity = 0;
    };
}