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

export module bench_internal;

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

    template <class T>
    using Store = inco::PagedStore<T, PB, MS>;

    // A bitmap + paged store filled directly, bypassing SparseSlotMap so every
    // iterator consumes the exact same physical layout.
    template <class T>
    struct Fixture {
        inco::HierarchicalBitmap bm;
        Store<T> store;

        // stride = 1 packed, 2 = 50%, 4 = 25%. Insert live*stride front-loaded,
        // then release the non-multiples so survivors spread across a
        // stride*-wide high-water.
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
        // wide, occupied leaf words holding 3..40 random bits and the occupied
        // words shuffled across the span -- worst case for a HW stride/stream
        // prefetcher. Returns the expected id-sum for the checker.
        std::uint64_t build_nightmare(std::size_t live, std::size_t spread,
                                      std::uint64_t seed) {
            const std::size_t M = live * spread;
            const std::size_t num_words = inco::ceil_div(M, 64);
            bm.grow(M);
            store.ensure(M);
            for (std::size_t i = 0; i < M; ++i) {
                const auto slot = bm.acquire();
                (void)slot; // set every bit; release the dead below
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

    // no iterator, no manual prefetch: raw leaf-word drain + gather. The floor.
    template <class T>
    std::uint64_t iter_baseline(Fixture<T>& f) {
        std::uint64_t sum = 0;
        const std::size_t words = inco::ceil_div(f.bm.capacity(), 64);
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

    // Drive any (bm, store)-constructible library iterator to a sum.
    template <class It, class T>
    std::uint64_t iter_drive(Fixture<T>& f) {
        std::uint64_t sum = 0;
        for (It it{f.bm, f.store}; it != std::default_sentinel; ++it)
            sum += sum_val((*it).value);
        return sum;
    }

    using inco::BitWalkIter;
    using inco::PageWalkIter;

    // The integrated public path: SparseSlotMap range-for (returns PageWalkIter).
    template <class T>
    void build_sparse(inco::SparseSlotMap<T>& m,
                      std::size_t live, std::size_t stride) {
        const std::size_t M = live * stride;
        m.reserve(M);
        std::vector<typename inco::SparseSlotMap<T>::key_type> keys;
        keys.reserve(M);
        for (std::size_t i = 0; i < M; ++i)
            keys.push_back(m.try_emplace(make_val<T>(i)).value());
        if (stride > 1)
            for (std::size_t i = 0; i < M; ++i)
                if (i % stride != 0) m.erase(keys[i]);
    }

    template <class T>
    std::uint64_t iter_sparse(inco::SparseSlotMap<T>& m) {
        std::uint64_t sum = 0;
        for (auto&& e : m) sum += sum_val(e.value);
        return sum;
    }

    constexpr bench::Pattern PATTERNS[] = {
        {"packed", 1}, {"half", 2}, {"quarter", 4},
    };

    // Shipped path: baseline floor vs PageWalkIter (bare + through SparseSlotMap).
    template <class T>
    void run_shipped(const char* elem) {
        for (const auto& p : PATTERNS) {
            Fixture<T> f;
            f.build(bench::N, p.stride);
            const std::uint64_t want = bench::expected_sum(p.stride);

            bench::check("baseline", want, iter_baseline<T>(f));
            bench::check("PageWalkIter", want,
                         iter_drive<PageWalkIter<T, Store<T> >, T>(f));

            bench::print_row(
                "baseline (no prefetch)", p.name, elem,
                bench::run(bench::N, [&] { return iter_baseline<T>(f); }));
            bench::print_row(
                "PageWalkIter", p.name, elem,
                bench::run(bench::N, [&] {
                    return iter_drive<PageWalkIter<T, Store<T> >, T>(f);
                }));

            inco::SparseSlotMap<T> m;
            build_sparse<T>(m, bench::N, p.stride);
            bench::check("SparseSlotMap", want, iter_sparse<T>(m));
            bench::print_row(
                "SparseSlotMap range-for", p.name, elem,
                bench::run(bench::N, [&] { return iter_sparse<T>(m); }));
        }
    }

    // Head-to-head of the library's iterator family on one layout.
    template <class T>
    void run_iterators(const char* elem) {
        for (const auto& p : PATTERNS) {
            Fixture<T> f;
            f.build(bench::N, p.stride);
            const std::uint64_t want = bench::expected_sum(p.stride);

            bench::check("BitWalkIter", want,
                         iter_drive<BitWalkIter<T, Store<T> >, T>(f));
            bench::check("PageWalkIter", want,
                         iter_drive<PageWalkIter<T, Store<T> >, T>(f));

            bench::print_row(
                "baseline (loop, no pf)", p.name, elem,
                bench::run(bench::N, [&] { return iter_baseline<T>(f); }));
            bench::print_row(
                "BitWalkIter", p.name, elem,
                bench::run(bench::N, [&] {
                    return iter_drive<BitWalkIter<T, Store<T> >, T>(f);
                }));
            bench::print_row(
                "PageWalkIter", p.name, elem,
                bench::run(bench::N, [&] {
                    return iter_drive<PageWalkIter<T, Store<T> >, T>(f);
                }));
        }
    }

    // Decode the live indices once so the prefetch-distance sweep measures only
    // the gather + prefetch, none of the bitmap-walk machinery.
    template <class T>
    std::vector<std::uint32_t> decode_indices(Fixture<T>& f) {
        std::vector<std::uint32_t> v;
        const std::size_t words = inco::ceil_div(f.bm.capacity(), 64);
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
        const std::uint64_t want =
            f.build_nightmare(bench::N, spread, 0xC0FFEEu);
        const auto idx = decode_indices(f);

        std::printf("  [nightmare: live=%zu span=%zu words_touched~=%zu]\n",
                    idx.size(), bench::N * spread,
                    idx.empty() ? 0 : (idx.back() / 64) + 1);
        std::fflush(stdout);

        bench::check("flat D=0", want, flat_prefetch<0, 3>(f, idx));
        bench::check("flat D=32", want, flat_prefetch<32, 3>(f, idx));
        bench::check("PageWalkIter", want,
                     iter_drive<PageWalkIter<T, Store<T> >, T>(f));

        bench::print_row(
            "flat baseline (D=0)", "nightmare", elem,
            bench::run(bench::N, [&] { return flat_prefetch<0, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=16 (t0)", "nightmare", elem,
            bench::run(bench::N, [&] { return flat_prefetch<16, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=32 (t0)", "nightmare", elem,
            bench::run(bench::N, [&] { return flat_prefetch<32, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=64 (t0)", "nightmare", elem,
            bench::run(bench::N, [&] { return flat_prefetch<64, 3>(f, idx); }));
        bench::print_row(
            "flat prefetch D=32 (nta)", "nightmare", elem,
            bench::run(bench::N, [&] { return flat_prefetch<32, 0>(f, idx); }));
        bench::print_row(
            "PageWalkIter (no pf)", "nightmare", elem,
            bench::run(bench::N, [&] {
                return iter_drive<PageWalkIter<T, Store<T> >, T>(f);
            }));
    }
}

extern "C++" int main() {
    std::printf(
        "slotmap internal (iterator) benchmark  (N=%zu live, warmup=%d reps=%d)\n",
        bench::N, bench::WARMUP, bench::REPS);
    std::printf("sizeof(Payload64)=%zu  min wall time over reps\n",
                sizeof(Payload64));

    bench::print_header("shipped path (uint32_t, 4B)");
    run_shipped<std::uint32_t>("u32");
    bench::print_header("shipped path (Payload64, 64B)");
    run_shipped<Payload64>("P64");

    bench::print_header("iterator family (uint32_t, cache-resident)");
    run_iterators<std::uint32_t>("u32");
    bench::print_header("iterator family (Payload64, memory-bound)");
    run_iterators<Payload64>("P64");

    bench::print_header("NIGHTMARE scatter, prefetch-distance sweep (P64, spread=8)");
    run_nightmare<Payload64>("P64", 8);
    bench::print_header("NIGHTMARE scatter, prefetch-distance sweep (P64, spread=16)");
    run_nightmare<Payload64>("P64", 16);

    std::printf("\n[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}
