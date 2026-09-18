module;

#include <bit>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include "../macros.h"
export module slotmap:iterators.batched_prefetch;
import :utils;
import :concepts;
import :iterators.entity;

namespace inco {
    // an attempt was made
    template <class T, class Store, class Finder>
    struct LeadPrefetcher {
        using word = typename Finder::word;
        static constexpr std::size_t WB = Finder::word_bits;

        const Finder* bm = nullptr;
        Store* store = nullptr;
        T* base = nullptr;
        std::size_t total_words = 0;
        std::size_t word_idx = 0;
        word cur = 0;
        bool done = false;

        LeadPrefetcher() = default;

        LeadPrefetcher(
            const Finder& b,
            Store& s,
            std::size_t start_word,
            word start_cur,
            T* start_base,
            bool start_done,
            std::size_t ahead
        ) noexcept : bm(&b), store(&s), base(start_base),
                     total_words(utils::ceil_div(b.capacity(), WB)),
                     word_idx(start_word), cur(start_cur), done(start_done
                     ) {
            for (std::size_t i = 0; i < ahead; ++i) pump();
        }

        FORCE_INLINE void pump() noexcept {
            if (done) return;
            utils::prefetch_read(base + std::countr_zero(cur));
            cur &= cur - 1;
            if (cur) return;
            do {
                if (++word_idx >= total_words) {
                    done = true;
                    return;
                }
                cur = bm->word_at(word_idx);
            } while (!cur);
            base = store->at(word_idx * WB);
        }
    };

    export template <class T, class Store, class Finder, std::size_t Ahead = 64>
        requires Storage<Store, T> && LiveBitmapView<Finder>
    class PrefetchPageWalkIter {
    public:
        using entry = SlotMapIteratorEntry<T>;
        using value_type = entry;
        using reference = entry;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        PrefetchPageWalkIter() = default;

        PrefetchPageWalkIter(const Finder& bm, Store& store) noexcept
            : _bitmap(&bm),
              _store(&store),
              _total_words(utils::ceil_div(bm.capacity(), Finder::word_bits)
              ) {
            if (_total_words) _current_word = bm.word_at(0);
            seek();
            _lead = LeadPrefetcher<T, Store, Finder>(
                bm,
                store,
                _word_idx,
                _current_word,
                _wbase,
                _done,
                Ahead);
        }

        entry operator*() const noexcept {
            const std::size_t idx = _word_idx * Finder::word_bits + _bit;
            return entry{.index = idx, .value = _wbase[_bit]};
        }

        PrefetchPageWalkIter& operator++() noexcept {
            _current_word &= _current_word - 1;
            if (_current_word) {
                _bit = static_cast<std::size_t>(
                    std::countr_zero(_current_word));
            } else {
                seek();
            }
            _lead.pump();
            return *this;
        }

        void operator++(int) noexcept { ++*this; }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _done;
        }

    private:
        FORCE_INLINE void seek() noexcept {
            while (!_current_word) {
                if (++_word_idx >= _total_words) {
                    _done = true;
                    return;
                }
                _current_word = _bitmap->word_at(_word_idx);
            }
            _wbase = _store->at(_word_idx * Finder::word_bits);
            _bit = static_cast<std::size_t>(std::countr_zero(_current_word));
        }

        const Finder* _bitmap = nullptr;
        Store* _store = nullptr;
        T* _wbase = nullptr;
        std::size_t _total_words = 0;
        std::size_t _word_idx = 0;
        std::size_t _bit = 0;
        typename Finder::word _current_word = 0;
        bool _done = false;
        LeadPrefetcher<T, Store, Finder> _lead{};
    };

    export template <class Finder, class Store, class
        Fn, std::size_t Ahead = 64>
        requires LiveBitmapView<Finder>
    void for_each_prefetched(const Finder& bm, Store& store, Fn&& fn) {
        using T = std::remove_pointer_t<decltype(store.at(std::size_t{}))>;
        constexpr std::size_t WB = Finder::word_bits;
        const std::size_t total = utils::ceil_div(bm.capacity(), WB);
        if (!total) return;

        const typename Finder::word w0 = bm.word_at(0);
        LeadPrefetcher<T, Store, Finder> lead(
            bm,
            store,
            0,
            w0,
            store.at(0),
            false,
            Ahead);

        for (std::size_t ci = 0; ci < total; ++ci) {
            typename Finder::word cw = bm.word_at(ci);
            if (!cw) continue;
            auto* base = store.at(ci * WB);
            do {
                const int b = std::countr_zero(cw);
                lead.pump();
                fn(ci * WB + static_cast<std::size_t>(b), base[b]);
                cw &= cw - 1;
            } while (cw);
        }
    }
}