module;
#include <cstddef>
#include <iterator>
#include "../macros.h"
export module slotmap.iterators:page_walk;
import :entity;
import slotmap.free;
import slotmap.utils;
import slotmap.concepts;

namespace inco {
    // a regular bit walk iterator, but the
    // page base pointer is resolved once per word via one at()
    // call and then indexed by bit.
    //
    // Requires page_slots % word_bits == 0 so a word is at the very least one page
    export template <class T, class Store, class Finder = HierarchicalBitmap,
        bool THING = true>
        requires Storage<Store, T> && LiveBitmapView<Finder>
    class PageWalkIter {
    public:
        using entry = SlotMapIteratorEntry<T>;
        using value_type = entry;
        using reference = entry;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        static_assert(
            Store::page_slots % Finder::word_bits == 0,
            "PageWalkIter requires each leaf word's slots to fit in one page");

        PageWalkIter() = default;

        PageWalkIter(const Finder& bm, Store& store) noexcept
            : _bitmap(&bm), _store(&store),
              _total_words(ceil_div(bm.capacity(),
                                    Finder::word_bits)) {
            if (_total_words) _current_word = bm.word_at(0);
            seek();
        }

        entry operator*() const noexcept {
            const std::size_t idx =
                _word_idx * Finder::word_bits + _bit;
            return entry{
                .index = idx,
                .value = _wbase[_bit]
            };
        }

        void advance() {
            while (_current_word && (_current_word & typename Finder::word{1})
                   == 0) {
                _current_word >>= 1;
                ++_bit;
            }
        }

        PageWalkIter& operator++() noexcept {
            constexpr auto WORD_1 = typename Finder::word{1};
            if constexpr (THING) {
                _current_word >>= 1;
                ++_bit;

                advance();

                if (_current_word) {
                    return *this;
                }
            } else {
                _current_word &= _current_word - 1;

                if (_current_word) {
                    _bit = static_cast<std::size_t>(
                        std::countr_zero(_current_word)
                    );
                    return *this;
                }
            }
            seek();
            return *this;
        }

        void operator++(int) noexcept { ++*this; }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _done;
        }

    private:
        // advance to the next nonempty word and resolve its page base once
        FORCE_INLINE
        void seek() noexcept {
            while (!_current_word) {
                if (++_word_idx >= _total_words) {
                    _done = true;
                    return;
                }
                _current_word = _bitmap->word_at(_word_idx);
            }
            _wbase = _store->at(_word_idx * Finder::word_bits);
            if constexpr (THING) {
                _bit = 0;
                advance();
            } else
                _bit = static_cast<std::size_t>(
                    std::countr_zero(_current_word));
        }

        const Finder* _bitmap = nullptr;
        Store* _store = nullptr;
        T* _wbase = nullptr;
        std::size_t _total_words = 0;
        std::size_t _word_idx = 0;
        std::size_t _bit = 0;
        typename Finder::word _current_word = 0;
        bool _done = false;
    };


    export template <class Finder, class Store, class Fn>
        requires LiveBitmapView<Finder>
    void for_each_walk(const Finder& bm, Store& store, Fn&& fn) {
        constexpr std::size_t WB = Finder::word_bits;
        const std::size_t total = ceil_div(bm.capacity(), WB);
        for (std::size_t ci = 0; ci < total; ++ci) {
            typename Finder::word w = bm.word_at(ci);
            if (!w) continue;
            auto* base = store.at(ci * WB);
            const std::size_t g = ci * WB;
            do {
                const int b = std::countr_zero(w);
                fn(g + static_cast<std::size_t>(b), base[b]);
                w &= w - 1;
            } while (w);
        }
    }
}