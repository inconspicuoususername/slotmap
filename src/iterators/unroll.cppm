module;
#include <iterator>
#include "../macros.h"
export module slotmap:iterators.unroll;
import :free;
import :utils;
import :concepts;
import :iterators.entity;

namespace inco {
    export template <class T, class Store, class Finder>
        requires Storage<Store, T> && LiveBitmapView<Finder>
    class UnrolledPageWalkIter {
    public:
        using entry = SlotMapIteratorEntry<T>;
        using value_type = entry;
        using reference = entry;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        static_assert(
            Store::page_slots % Finder::word_bits == 0,
            "UnrolledPageWalkIter requires each leaf word's slots in one page");

        FORCE_INLINE UnrolledPageWalkIter() = default;

        FORCE_INLINE UnrolledPageWalkIter(const Finder& bm,
                                          Store& store) noexcept
            : _bitmap(&bm), _store(&store),
              _total_words(utils::ceil_div(bm.capacity(), Finder::word_bits)) {
            refill();
        }

        FORCE_INLINE entry operator*() const noexcept {
            const std::size_t p = _pos[_index];
            return entry{.index = _gbase + p, .value = _base[p]};
        }

        FORCE_INLINE void operator++() noexcept {
            if (++_index >= _num_prefilled) refill();
        }

        FORCE_INLINE void operator++(int) noexcept { ++*this; }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _done;
        }

    private:
        FORCE_INLINE void refill() noexcept {
            _index = 0;
            while (_word_idx < _total_words) {
                // curse of the turbofish
                typename Finder::word word = _bitmap->word_at(_word_idx);
                const std::size_t wordidx = _word_idx++;
                if (!word) continue;
                _base = _store->at(wordidx * Finder::word_bits);
                _gbase = wordidx * Finder::word_bits;
                int n = 0;
                do {
                    _pos[n++] =
                        static_cast<std::uint8_t>(std::countr_zero(word));
                    word &= word - 1;
                } while (word);
                _num_prefilled = n;
                return;
            }
            _done = true;
            _num_prefilled = 0;
        }

        const Finder* _bitmap = nullptr;
        Store* _store = nullptr;
        T* __restrict _base = nullptr;
        std::size_t _total_words = 0;
        std::size_t _word_idx = 0;
        std::size_t _gbase = 0;
        int _index = 0;
        int _num_prefilled = 0;
        bool _done = false;
        std::uint8_t _pos[Finder::word_bits]{};
    };


    // attempt to solve blsr load dependency (thank you x86)
    export template <class Finder, class Store, class Fn>
        requires LiveBitmapView<Finder>
    void for_each_expanded(const Finder& bitmap, Store& store, Fn&& fn) {
        constexpr auto WORD_BITS = Finder::word_bits;
        constexpr auto WORD_1 = typename Finder::word{1};
        const std::size_t total = utils::ceil_div(bitmap.capacity(), WORD_BITS);

        for (std::size_t cap_i = 0; cap_i < total; ++cap_i) {
            typename Finder::word word = bitmap.word_at(cap_i);
            if (!word) continue;

            const std::size_t full_offset = cap_i * WORD_BITS;
            auto* base = store.at(full_offset);
            const std::size_t g = full_offset;

            for (int b = 0; b < WORD_BITS; ++b)
                if ((word >> b) & WORD_1)
                    fn(g + static_cast<std::size_t>(b), base[b]);
        }
    }

    export template <class T, class Finder, class Store, class Fn, int K = 8>
        requires LiveBitmapView<Finder> && IterativeLambda<Fn, T>
    void for_each_unrolled(const Finder& bitmap, Store& store, Fn&& fn) {
        const std::size_t total =
            utils::ceil_div(bitmap.capacity(), Finder::word_bits);

        for (std::size_t cap_i = 0; cap_i < total; ++cap_i) {
            typename Finder::word word = bitmap.word_at(cap_i);
            if (!word) continue;

            const std::size_t full_offset = cap_i * Finder::word_bits;
            auto* base = store.at(full_offset);

            while (std::popcount(word) >= K) {
                int unroll[K];
#pragma GCC unroll 16
                for (int i = 0; i < K; ++i) {
                    unroll[i] = std::countr_zero(word);
                    word &= word - 1;
                }
#pragma GCC unroll 16
                for (int i = 0; i < K; ++i)
                    fn(full_offset + static_cast<std::size_t>(unroll[i]),
                       base[unroll[i]]);
            }
            while (word) {
                const auto bits = std::countr_zero(word);
                fn(full_offset + static_cast<std::size_t>(bits), base[bits]);
                word &= word - 1;
            }
        }
    }

    export template <class Finder, class Store, class Fn, int K = 8>
        requires LiveBitmapView<Finder>
    void for_each_unrolled2(const Finder& bm, Store& store, Fn&& fn) {
        const std::size_t total = utils::ceil_div(bm.capacity(), Finder::word_bits);

        for (std::size_t cap_i = 0; cap_i < total; ++cap_i) {
            typename Finder::word word = bm.word_at(cap_i);
            if (!word) continue;

            const std::size_t full_offset = cap_i * Finder::word_bits;
            auto* base = store.at(full_offset);

            while (std::popcount(word) >= 8) {
                int unroll[8];
#pragma GCC unroll 8
                for (int i = 0; i < 8; ++i)
                    unroll[i] = std::countr_zero(_pdep_u64(1ull << i, word));
                word = _pdep_u64(~0ull << 8, word);
#pragma GCC unroll 8
                for (int i = 0; i < 8; ++i)
                    fn(full_offset +
                       static_cast<std::size_t>(unroll[i]),
                       base[unroll[i]]);
            }
            while (word) {
                const auto bits = std::countr_zero(word);
                fn(full_offset + static_cast<std::size_t>(bits), base[bits]);
                word &= word - 1;
            }
        }
    }
}
