module;

#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

#include "../macros.h"

export module slotmap:free.defer;
import :free.bitmap;
import :utils;

namespace inco {
    struct SlotRing {
        static constexpr std::size_t cap = 1024;

        SlotRing() = default;
        SlotRing(const SlotRing&) = default;
        SlotRing& operator=(const SlotRing&) = default;

        SlotRing(SlotRing&& o) noexcept
            : _buf(std::move(o._buf)),
              _head(std::exchange(o._head, 0)),
              _count(std::exchange(o._count, 0)) {}

        SlotRing& operator=(SlotRing&& o) noexcept {
            if (this != &o) {
                _buf = std::move(o._buf);
                _head = std::exchange(o._head, 0);
                _count = std::exchange(o._count, 0);
            }
            return *this;
        }

        [[nodiscard]] FORCE_INLINE bool full() const noexcept {
            return _count >= cap;
        }

        [[nodiscard]] FORCE_INLINE bool empty() const noexcept {
            return _count == 0;
        }

        FORCE_INLINE void push(std::size_t slot) noexcept {
            if (_buf.empty()) _buf.resize(cap);
            _buf[(_head + _count) % cap] = slot;
            ++_count;
        }

        FORCE_INLINE std::size_t pop() noexcept {
            const std::size_t slot = _buf[_head];
            _head = (_head + 1) % cap;
            --_count;
            return slot;
        }

    private:
        std::vector<std::size_t> _buf{};
        std::size_t _head = 0;
        std::size_t _count = 0;
    };


    // All of the "deferred" versions of the bitmap are different variants of the same idea
    // compared to a intrusive freelist, the bitmap is O(log64n I think?), while the freelist is O(1)
    // in inserts and erases. A simple way to fix this is to add freelist esque mechanism on top of the bitmap
    //
    // A similar idea is used by this implementation (https://github.com/SergeyMakeev/SlotMap)
    // where he waits until his freelist has at least 64(?) free indices, before popping off of it
    class DeferredBitmap {
    public:
        using word = HierarchicalBitmap::word;
        static constexpr int word_bits = HierarchicalBitmap::word_bits;
        static constexpr std::size_t npos = HierarchicalBitmap::npos;

        DeferredBitmap() = default;
        DeferredBitmap(const DeferredBitmap&) = default;
        DeferredBitmap& operator=(const DeferredBitmap&) = default;

        DeferredBitmap(DeferredBitmap&& o) noexcept
            : _bitmap(std::move(o._bitmap)),
              _frontier(std::exchange(o._frontier, 0)) {}

        DeferredBitmap& operator=(DeferredBitmap&& o) noexcept {
            if (this != &o) {
                _bitmap = std::move(o._bitmap);
                _frontier = std::exchange(o._frontier, 0);
            }
            return *this;
        }

        void grow(const std::size_t slots) { _bitmap.grow(slots); }

        [[nodiscard]] std::size_t capacity() const noexcept {
            return _bitmap.capacity();
        }

        [[nodiscard]] word word_at(const std::size_t index) const {
            return _bitmap.word_at(index);
        }

    protected:
        HierarchicalBitmap _bitmap{};
        std::size_t _frontier = 0;
    };

    export class BoundedDeferBitmap : public DeferredBitmap {
    public:
        [[nodiscard]] std::size_t acquire() noexcept {
            if (!_ring.empty()) return _bitmap.occupy(_ring.pop());
            if (_frontier < _bitmap.capacity())
                return _bitmap.occupy(
                    _frontier++);
            return _bitmap.acquire();
        }

        void release(std::size_t slot) noexcept {
            _bitmap.release(slot);
            if (!_ring.full()) _ring.push(slot);
        }

    private:
        SlotRing _ring{};
    };

    // frankenstein thing
    // use slots at frontier and only recycle old free slots after threshold
    export class UnboundedDeferBitmap : public DeferredBitmap {
    public:
        [[nodiscard]] std::size_t acquire() noexcept {
            if (_defer.size() > defer_threshold) {
                const auto slot = _defer.front();
                _defer.pop_front();
                _bitmap.set_leaf_bit(slot);
                return slot;
            }
            if (_frontier < _bitmap.capacity()) {
                const std::size_t slot = _frontier++;
                _bitmap.set_leaf_bit(slot);
                return slot;
            }
            return npos;
        }

        void release(std::size_t slot) noexcept {
            _bitmap.clear_leaf_bit(slot);
            _defer.push_back(slot);
        }

    private:
        static constexpr std::size_t defer_threshold = 64; // age before reuse
        std::deque<std::size_t> _defer; // unbounded FIFO of frees
    };

    // A variant of the regular bitmap that also has info about "reserved" allocations
    // so recently released slots are left to rot for a little bit
    export class LiveAllocBitmap {
    public:
        using word = HierarchicalBitmap::word;
        static constexpr int word_bits = HierarchicalBitmap::word_bits;
        static constexpr std::size_t npos = HierarchicalBitmap::npos;

        LiveAllocBitmap() = default;
        LiveAllocBitmap(const LiveAllocBitmap&) = default;
        LiveAllocBitmap& operator=(const LiveAllocBitmap&) = default;

        LiveAllocBitmap(LiveAllocBitmap&& o) noexcept
            : _alloc(std::move(o._alloc)),
              _live(std::move(o._live)),
              _ring(std::move(o._ring)),
              _frontier(std::exchange(o._frontier, 0)) {}

        LiveAllocBitmap& operator=(LiveAllocBitmap&& o) noexcept {
            if (this != &o) {
                _alloc = std::move(o._alloc);
                _live = std::move(o._live);
                _ring = std::move(o._ring);
                _frontier = std::exchange(o._frontier, 0);
            }
            return *this;
        }

        [[nodiscard]] FORCE_INLINE std::size_t acquire() noexcept {
            std::size_t slot;
            if (!_ring.empty()) {
                // slot already in ring buffer
                slot = _ring.pop();
            } else if (_frontier < _alloc.capacity()) {
                slot = _alloc.occupy(_frontier++);
            } else {
                // TODO optimize
                slot = _alloc.acquire();
                if (slot == npos) return npos;
            }
            set_live(slot);
            return slot;
        }

        FORCE_INLINE void release(std::size_t slot) noexcept {
            clear_live(slot);
            if (!_ring.full()) _ring.push(slot);
            else _alloc.release(slot);
        }

        FORCE_INLINE void grow(std::size_t slots) {
            _alloc.grow(slots);
            _live.resize(utils::ceil_div(slots, word_bits), 0);
        }

        [[nodiscard]] FORCE_INLINE std::size_t capacity() const noexcept {
            return _alloc.capacity();
        }

        [[nodiscard]] FORCE_INLINE word word_at(std::size_t index) const {
            return _live[index];
        }

    private:
        FORCE_INLINE void set_live(std::size_t slot) noexcept {
            _live[slot / word_bits] |= (word{1} << (slot % word_bits));
        }

        FORCE_INLINE void clear_live(std::size_t slot) noexcept {
            _live[slot / word_bits] &= ~(word{1} << (slot % word_bits));
        }

        HierarchicalBitmap _alloc{};
        std::vector<word> _live{};

        SlotRing _ring{};

        std::size_t _frontier = 0;
    };
}