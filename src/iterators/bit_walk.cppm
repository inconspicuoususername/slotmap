module;
#include <bit>
#include <cstddef>
#include <iterator>
export module slotmap.iterators:bit_walk;
import :entity;
import slotmap.free;
import slotmap.utils;
import slotmap.concepts;

namespace inco {
    // Identitcal to the page walk iterator
    // walk but PageWalk does the page lookup once per word (a word's 64 slots share one
    // page), so its faster in iteration
    export template<class T, class Store>
        requires Storage<Store, T>
    class BitWalkIter {
    public:
        using entry = SlotMapIteratorEntry<T>;
        using value_type = entry;
        using reference = entry;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        BitWalkIter() = default;

        BitWalkIter(const HierarchicalBitmap &bm, Store &store) noexcept
            : _bitmap(&bm), _store(&store),
              _total_words(ceil_div(bm.capacity(),
                                    HierarchicalBitmap::word_bits)) {
            if (_total_words) _current_word = bm.word_at(0);
            seek();
        }

        entry operator*() const noexcept {
            return entry{.index = _cur, .value = *_store->at(_cur)};
        }

        BitWalkIter &operator++() noexcept {
            _current_word &= _current_word - 1;
            seek();
            return *this;
        }

        void operator++(int) noexcept { ++*this; }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _done;
        }

    private:
        void seek() noexcept {
            while (!_current_word) {
                if (++_word_idx >= _total_words) {
                    _done = true;
                    return;
                }
                _current_word = _bitmap->word_at(_word_idx);
            }
            _cur = _word_idx * HierarchicalBitmap::word_bits +
                   static_cast<std::size_t>(std::countr_zero(_current_word));
        }

        const HierarchicalBitmap *_bitmap = nullptr;
        Store *_store = nullptr;
        std::size_t _total_words = 0;
        std::size_t _word_idx = 0;
        std::size_t _cur = 0;
        HierarchicalBitmap::word _current_word = 0;
        bool _done = false;
    };
}