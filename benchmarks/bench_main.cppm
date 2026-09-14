module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <random>
#include <vector>

#include "bench_common.hpp"

export module bench_main;

import slotmap;
import slotmap.utils;
import slotmap.free;
import slotmap.storage;
import slotmap.iterators;

using bench::Payload64;
using bench::make_val;
using bench::sum_val;

namespace {
    constexpr std::size_t PB = 16 * 1024;
    constexpr std::size_t MS = 32;

    // A bitmap + paged store filled directly, bypassing SparseSlotMap so every
    // iterator style consumes the exact same physical layout.
    template <class T>
    struct Fixture {
        slotmap::HierarchicalBitmap bm;
        slotmap::PagedStore<T, PB, MS> store;

        // live = elements that survive; stride = 1 packed, 2 = 50%, 4 = 25%.
        // Insert live*stride front-loaded, then release the non-multiples of
        // stride so the survivors are spread across a stride*-wide high-water.
        void build(std::size_t live, std::size_t stride) {
            const std::size_t M = live * stride;
            bm.grow(M);
            store.ensure(M);
            for (std::size_t i = 0; i < M; ++i) {
                const auto slot = bm.acquire();
                (void)slot; // front-loads to i
                store.construct(i, make_val<T>(i));
            }
            if (stride > 1)
                for (std::size_t i = 0; i < M; ++i)
                    if (i % stride != 0) {
                        store.destroy(i);
                        bm.release(i);
                    }
        }

        // Nightmare layout: `live` survivors scattered over a span `live*spread`
        // wide, with occupied leaf words holding 3..40 random bits and the
        // occupied words themselves shuffled across the span. Big, irregular
        // gaps between consecutive live elements -- worst case for a HW
        // stride/stream prefetcher. Returns the expected id-sum for the checker.
        std::uint64_t build_nightmare(std::size_t live, std::size_t spread,
                                      std::uint64_t seed) {
            const std::size_t M = live * spread;
            const std::size_t num_words = ic::ceil_div(M, 64);
            bm.grow(M);
            store.ensure(M);
            for (std::size_t i = 0; i < M; ++i) {
                const auto slot = bm.acquire();
                // set every bit; release the dead below
                (void)slot;
            }

            std::vector<std::uint8_t> is_live(M, 0);
            std::vector<std::size_t> words(num_words);
            std::iota(words.begin(), words.end(), std::size_t{0});
            std::mt19937_64 rng(seed);
            std::shuffle(words.begin(), words.end(), rng);

            std::array<std::uint8_t, 64> perm{};
            std::iota(perm.begin(), perm.end(), std::uint8_t{0});

            std::size_t placed = 0, wi = 0;
            while (placed < live && wi < num_words) {
                const std::size_t w = words[wi++];
                std::size_t k = 3 + (rng() % 38); // 3..40 bits this word
                if (k > live - placed) k = live - placed;
                for (std::size_t j = 0; j < k; ++j) {
                    const std::size_t r = j + rng() % (64 - j);
                    std::swap(perm[j], perm[r]);
                    is_live[w * 64 + perm[j]] = 1;
                    ++placed;
                }
            }

            std::uint64_t expected = 0;
            for (std::size_t i = 0; i < M; ++i) {
                if (is_live[i]) {
                    store.construct(i, make_val<T>(i));
                    expected += i;
                } else {
                    bm.release(i);
                }
            }
            return expected;
        }
    };

    // no manual prefetch: raw leaf-word drain + gather. The floor we compare to.
    template <class T>
    std::uint64_t iter_baseline(Fixture<T>& f) {
        std::uint64_t sum = 0;
        const std::size_t words = ic::ceil_div(f.bm.capacity(), 64);
        for (std::size_t w = 0; w < words; ++w) {
            auto word = f.bm.word_at(w);
            while (word) {
                const int b = std::countr_zero(word);
                const std::size_t idx = w * 64 + static_cast<std::size_t>(b);
                sum += sum_val(*f.store.at(idx));
                word &= word - 1;
            }
        }
        return sum;
    }

    // The integrated, ergonomic path: SparseSlotMap range-for, which now returns
    // PageWalkIter. Built through the public API to exercise the shipped iterator.
    template <class T>
    void build_sparse(slotmap::SparseSlotMap<T>& m,
                      std::size_t live, std::size_t stride) {
        const std::size_t M = live * stride;
        m.reserve(M);
        std::vector<typename slotmap::SparseSlotMap<T>::key_type> keys;
        keys.reserve(M);
        for (std::size_t i = 0; i < M; ++i)
            keys.push_back(m.try_emplace(make_val<T>(i)).value());
        if (stride > 1)
            for (std::size_t i = 0; i < M; ++i)
                if (i % stride != 0) m.erase(keys[i]);
    }

    template <class T>
    std::uint64_t iter_sparse(slotmap::SparseSlotMap<T>& m) {
        std::uint64_t sum = 0;
        for (auto&& e : m) sum += sum_val(e.value);
        return sum;
    }

    // ---- rewrite prototypes (measured against the baseline gather) ----

    // No buffer, no prefetch: carry (_w, _word_idx), bit-walk in operator++.
    // The tightest an iterator can be -- baseline logic, iterator shape.
    template <class T>
    class BitWalkIter {
    public:
        using entry = slotmap::SlotMapIteratorEntry<T>;

        BitWalkIter() = default;

        BitWalkIter(const slotmap::HierarchicalBitmap& bm,
                    slotmap::PagedStore<T, PB, MS>& store) noexcept
            : _bm(&bm), _store(&store),
              _words(ic::ceil_div(bm.capacity(), 64)) {
            if (_words) _w = bm.word_at(0);
            seek();
        }

        entry operator*() const noexcept {
            return entry{_cur, *_store->at(_cur)};
        }

        BitWalkIter& operator++() noexcept {
            _w &= _w - 1;
            seek();
            return *this;
        }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _done;
        }

    private:
        void seek() noexcept {
            while (!_w) {
                if (++_word_idx >= _words) {
                    _done = true;
                    return;
                }
                _w = _bm->word_at(_word_idx);
            }
            _cur = _word_idx * 64 +
                   static_cast<std::size_t>(std::countr_zero(_w));
        }

        const slotmap::HierarchicalBitmap* _bm = nullptr;
        slotmap::PagedStore<T, PB, MS>* _store = nullptr;
        std::size_t _words = 0, _word_idx = 0, _cur = 0;
        std::uint64_t _w = 0;
        bool _done = false;
    };

    // #1: BitWalk + __restrict on the stored pointers, so the compiler can
    // assume _bm/_store don't alias and keep the decoded state in registers.
    template <class T>
    class BitWalkIterR {
    public:
        using entry = slotmap::SlotMapIteratorEntry<T>;

        BitWalkIterR() = default;

        BitWalkIterR(const slotmap::HierarchicalBitmap& bm,
                     slotmap::PagedStore<T, PB, MS>& store) noexcept
            : _bm(&bm), _store(&store),
              _words(ic::ceil_div(bm.capacity(), 64)) {
            if (_words) _w = bm.word_at(0);
            seek();
        }

        entry operator*() const noexcept {
            return entry{_cur, *_store->at(_cur)};
        }

        BitWalkIterR& operator++() noexcept {
            _w &= _w - 1;
            seek();
            return *this;
        }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _done;
        }

    private:
        void seek() noexcept {
            while (!_w) {
                if (++_word_idx >= _words) {
                    _done = true;
                    return;
                }
                _w = _bm->word_at(_word_idx);
            }
            _cur = _word_idx * 64 +
                   static_cast<std::size_t>(std::countr_zero(_w));
        }

        const slotmap::HierarchicalBitmap* __restrict _bm = nullptr;
        slotmap::PagedStore<T, PB, MS>* __restrict _store = nullptr;
        std::size_t _words = 0, _word_idx = 0, _cur = 0;
        std::uint64_t _w = 0;
        bool _done = false;
    };

    // #2: page-aware. A leaf word's 64 slots live inside one page, so hoist the
    // page base pointer once per word and index it by bit -- kills the
    // pages_[slot>>shift] load that at() does on every element.
    template <class T>
    class PageWalkIter {
    public:
        using entry = slotmap::SlotMapIteratorEntry<T>;

        PageWalkIter() = default;

        PageWalkIter(const slotmap::HierarchicalBitmap& bm,
                     slotmap::PagedStore<T, PB, MS>& store) noexcept
            : _bm(&bm), _store(&store),
              _words(ic::ceil_div(bm.capacity(), 64)) {
            if (_words) _w = bm.word_at(0);
            seek();
        }

        entry operator*() const noexcept {
            const std::size_t idx = _word_idx * 64 + _bit;
            return entry{idx, _wbase[_bit]};
        }

        PageWalkIter& operator++() noexcept {
            _w &= _w - 1;
            if (_w) {
                _bit = static_cast<std::size_t>(std::countr_zero(_w));
                return *this; // same word/page, just the next bit
            }
            seek();
            return *this;
        }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _done;
        }

    private:
        void seek() noexcept {
            while (!_w) {
                if (++_word_idx >= _words) {
                    _done = true;
                    return;
                }
                _w = _bm->word_at(_word_idx);
            }
            _wbase = _store->at(_word_idx * 64);
            // page base for this word's slots
            _bit = static_cast<std::size_t>(std::countr_zero(_w));
        }

        const slotmap::HierarchicalBitmap* __restrict _bm = nullptr;
        slotmap::PagedStore<T, PB, MS>* __restrict _store = nullptr;
        T* __restrict _wbase = nullptr;
        std::size_t _words = 0, _word_idx = 0, _bit = 0;
        std::uint64_t _w = 0;
        bool _done = false;
    };

    // Decode a batch, then hot path is *_p++ over the buffer (no per-element
    // at(cursor) recompute). Prefetch is a compile-time flag.
    template <class T, bool Prefetch, std::size_t Batch = 64>
    class BatchPtrIter {
    public:
        using entry = slotmap::SlotMapIteratorEntry<T>;

        BatchPtrIter() = default;

        BatchPtrIter(const slotmap::HierarchicalBitmap& bm,
                     slotmap::PagedStore<T, PB, MS>& store) noexcept
            : _bm(&bm), _store(&store),
              _words(ic::ceil_div(bm.capacity(), 64)) {
            if (_words) _w = bm.word_at(0);
            refill();
        }

        entry operator*() const noexcept {
            const std::size_t idx = *_p;
            return entry{idx, *_store->at(idx)};
        }

        BatchPtrIter& operator++() noexcept {
            if (++_p == _end) refill();
            return *this;
        }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _p == _end;
        }

    private:
        void refill() noexcept {
            std::size_t n = 0;
            bool full = false;
            while (!full) {
                while (_w) {
                    const int b = std::countr_zero(_w);
                    _buf[n++] = static_cast<std::uint32_t>(_word_idx * 64 + b);
                    _w &= _w - 1;
                    if (n == Batch) {
                        full = true;
                        break;
                    }
                }
                if (full) break;
                if (++_word_idx >= _words) break;
                _w = _bm->word_at(_word_idx);
            }
            _p = _buf.data();
            _end = _buf.data() + n;
            if constexpr (Prefetch)
                for (std::size_t i = 0; i < n; ++i)
                    ic::prefetch_read(_store->at(_buf[i]));
        }

        const slotmap::HierarchicalBitmap* _bm = nullptr;
        slotmap::PagedStore<T, PB, MS>* _store = nullptr;
        std::array<std::uint32_t, Batch> _buf{};
        const std::uint32_t* _p = nullptr;
        const std::uint32_t* _end = nullptr;
        std::size_t _words = 0, _word_idx = 0;
        std::uint64_t _w = 0;
    };

    template <class It, class T>
    std::uint64_t iter_generic(Fixture<T>& f) {
        std::uint64_t sum = 0;
        for (It it{f.bm, f.store}; it != std::default_sentinel; ++it)
            sum += sum_val((*it).value);
        return sum;
    }

    struct Pattern {
        const char* name;
        std::size_t stride;
    };

    constexpr Pattern PATTERNS[] = {
        {"packed", 1}, {"half", 2}, {"quarter", 4},
    };

    // Every style must visit exactly the live set. Survivors are {0, s, 2s, ...,
    // (N-1)s}, so the id-sum is s * (0+1+...+(N-1)). Any re-read or skip fails this.
    std::uint64_t expected_sum(std::size_t stride) {
        const std::uint64_t n = bench::N;
        return static_cast<std::uint64_t>(stride) * (n * (n - 1) / 2);
    }

    void check(const char* name, std::uint64_t want, std::uint64_t got) {
        if (want != got)
            std::printf("  !! CHECK FAILED %-26s want=%llu got=%llu\n",
                        name,
                        static_cast<unsigned long long>(want),
                        static_cast<unsigned long long>(got));
    }

    template <class T>
    void run_elem(const char* elem) {
        for (const auto& p : PATTERNS) {
            Fixture<T> f;
            f.build(bench::N, p.stride);
            const std::uint64_t want = expected_sum(p.stride);

            // correctness gate (single shot, untimed) before trusting any timing
            check("baseline", want, iter_baseline<T>(f));
            check("PageWalkIter", want, iter_generic<PageWalkIter<T>, T>(f));

            bench::print_row(
                "baseline (no prefetch)",
                p.name,
                elem,
                bench::run(bench::N, [&] { return iter_baseline<T>(f); }));
            bench::print_row(
                "PageWalkIter",
                p.name,
                elem,
                bench::run(bench::N,
                           [&] {
                               return iter_generic<PageWalkIter<T>, T>(f);
                           }));

            slotmap::SparseSlotMap<T> m;
            build_sparse<T>(m, bench::N, p.stride);
            check("SparseSlotMap", want, iter_sparse<T>(m));
            bench::print_row(
                "SparseSlotMap range-for",
                p.name,
                elem,
                bench::run(bench::N, [&] { return iter_sparse<T>(m); }));
        }
    }

    // Head-to-head of the rewrite candidates vs the existing styles.
    template <class T>
    void run_rewrites(const char* elem) {
        for (const auto& p : PATTERNS) {
            Fixture<T> f;
            f.build(bench::N, p.stride);
            const std::uint64_t want = expected_sum(p.stride);

            check("BitWalkIter", want, iter_generic<BitWalkIter<T>, T>(f));
            check("BitWalkIterR", want, iter_generic<BitWalkIterR<T>, T>(f));
            check("PageWalkIter", want, iter_generic<PageWalkIter<T>, T>(f));

            bench::print_row(
                "baseline (loop, no pf)",
                p.name,
                elem,
                bench::run(bench::N, [&] { return iter_baseline<T>(f); }));
            bench::print_row(
                "BitWalkIter (plain)",
                p.name,
                elem,
                bench::run(bench::N,
                           [&] { return iter_generic<BitWalkIter<T>, T>(f); }));
            bench::print_row(
                "BitWalkIterR (#1 restrict)",
                p.name,
                elem,
                bench::run(bench::N,
                           [&] {
                               return iter_generic<BitWalkIterR<T>, T>(f);
                           }));
            bench::print_row(
                "PageWalkIter (#2 page-base)",
                p.name,
                elem,
                bench::run(bench::N,
                           [&] {
                               return iter_generic<PageWalkIter<T>, T>(f);
                           }));
        }
    }

    // Decode the live indices once so the prefetch-distance sweep measures only
    // the gather + prefetch, none of the bitmap-walk machinery.
    template <class T>
    std::vector<std::uint32_t> decode_indices(Fixture<T>& f) {
        std::vector<std::uint32_t> v;
        const std::size_t words = ic::ceil_div(f.bm.capacity(), 64);
        for (std::size_t w = 0; w < words; ++w) {
            auto word = f.bm.word_at(w);
            while (word) {
                v.push_back(static_cast<std::uint32_t>(
                    w * 64 + static_cast<std::size_t>(std::countr_zero(word))));
                word &= word - 1;
            }
        }
        return v;
    }

    template <int Loc>
    inline void pf(const void* p) noexcept {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(p, 0, Loc);
#else
        (void)p;
#endif
    }

    // Textbook SW-prefetch loop: prefetch element i+D while consuming i.
    template <std::size_t D, int Loc, class T>
    std::uint64_t flat_prefetch(Fixture<T>& f,
                                const std::vector<std::uint32_t>& idx) {
        std::uint64_t sum = 0;
        const std::size_t n = idx.size();
        for (std::size_t i = 0; i < n; ++i) {
            if constexpr (D > 0)
                if (i + D < n) pf<Loc>(f.store.at(idx[i + D]));
            sum += sum_val(*f.store.at(idx[i]));
        }
        return sum;
    }

    template <class T>
    void run_nightmare(const char* elem, std::size_t spread) {
        Fixture<T> f;
        const std::uint64_t want = f.build_nightmare(
            bench::N,
            spread,
            0xC0FFEEu);
        const auto idx = decode_indices(f);

        // sanity: the scatter really did span ~spread*N and fragment the words
        std::printf("  [nightmare: live=%zu span=%zu words_touched~=%zu]\n",
                    idx.size(),
                    bench::N * spread,
                    idx.empty() ? 0 : (idx.back() / 64) + 1);
        std::fflush(stdout);

        check("flat D=0", want, flat_prefetch<0, 3>(f, idx));
        check("flat D=32", want, flat_prefetch<32, 3>(f, idx));
        check("BitWalk", want, iter_generic<BitWalkIter<T>, T>(f));

        bench::print_row(
            "flat baseline (D=0)",
            "nightmare",
            elem,
            bench::run(bench::N, [&] { return flat_prefetch<0, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=8 (t0)",
            "nightmare",
            elem,
            bench::run(bench::N, [&] { return flat_prefetch<8, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=16 (t0)",
            "nightmare",
            elem,
            bench::run(bench::N, [&] { return flat_prefetch<16, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=32 (t0)",
            "nightmare",
            elem,
            bench::run(bench::N, [&] { return flat_prefetch<32, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=64 (t0)",
            "nightmare",
            elem,
            bench::run(bench::N, [&] { return flat_prefetch<64, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=128 (t0)",
            "nightmare",
            elem,
            bench::run(bench::N,
                       [&] { return flat_prefetch<128, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=32 (nta)",
            "nightmare",
            elem,
            bench::run(bench::N, [&] { return flat_prefetch<32, 0>(f, idx); }));

        // the shipped iterator on the same nightmare layout
        bench::print_row(
            "PageWalkIter (no pf)",
            "nightmare",
            elem,
            bench::run(bench::N,
                       [&] { return iter_generic<PageWalkIter<T>, T>(f); }));
    }
}

// Defined in refs.cpp (plain TU, no modules).
extern "C" void bench_refs();

extern "C++" int main() {
    std::printf(
        "slotmap iteration benchmark  (N=%zu live, warmup=%d reps=%d)\n",
        bench::N,
        bench::WARMUP,
        bench::REPS);
    std::printf("sizeof(Payload64)=%zu  min wall time over reps\n",
                sizeof(Payload64));

    bench::print_header("shipped path (uint32_t, 4B)");
    run_elem<std::uint32_t>("u32");

    bench::print_header("shipped path (Payload64, 64B)");
    run_elem<Payload64>("P64");

    bench::print_header("iterator rewrites (uint32_t, cache-resident regime)");
    run_rewrites<std::uint32_t>("u32");
    bench::print_header("iterator rewrites (Payload64, memory-bound regime)");
    run_rewrites<Payload64>("P64");

    bench::print_header(
        "NIGHTMARE scatter, prefetch-distance sweep (P64, spread=8)");
    run_nightmare<Payload64>("P64", 8);
    bench::print_header(
        "NIGHTMARE scatter, prefetch-distance sweep (P64, spread=16)");
    run_nightmare<Payload64>("P64", 16);

    bench_refs();

    std::printf("\n[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}