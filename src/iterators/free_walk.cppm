module;
#include <cstddef>
#include <iterator>
#include <vector>
export module slotmap:iterators.free_walk;
import :iterators.entity;
import :free;
import :concepts;

namespace inco {
    export template <class T, class Store, class Finder = FreeList>
        requires Storage<Store, T> && LiveViewSlots<Finder>
    class FreeListIter {
    public:
        using entry = SlotMapIteratorEntry<T>;
        using value_type = entry;
        using reference = entry;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        FreeListIter() = default;

        FreeListIter(
            const Finder& finder,
            Store& store
        ):
            _finder(&finder),
            _store(&store),
            _cap(finder.capacity()) {
            seek();
        }

        entry operator*() const noexcept {
            return entry{.index = _idx, .value = *_store->at(_idx)};
        }

        FreeListIter& operator++() noexcept {
            ++_idx;
            seek();
            return *this;
        }

        void operator++(int) noexcept { ++*this; }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _idx >= _cap;
        }

    private:
        void seek() noexcept {
            while (_idx < _cap && !_finder->is_live(_idx)) ++_idx;
        }

        const Finder* _finder = nullptr;
        Store* _store = nullptr;
        std::size_t _cap = 0;
        std::size_t _idx = 0;
    };
}
