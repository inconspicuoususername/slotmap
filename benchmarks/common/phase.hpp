#pragma once
#include <cstdint>
#include <random>
#include <vector>

#include "common.hpp"

namespace bench::phase {
    // Survivor fixture for iterate/find/churn: insert M = live*stride, then
    // thin to the survivor set {0, s, 2s, ...}. Built once, not timed.
    template <class Ad, class T>
    struct Survivors {
        typename Ad::template Map<T> m;
        std::vector<typename Ad::template Key<T> > keys;
        // the live survivors
        std::uint64_t expected; // their id-sum

        Survivors(std::size_t live, std::size_t stride)
            : m(Ad::template make<T>()) {
            const std::size_t M = live * stride;
            std::vector<typename Ad::template Key<T> > all;
            all.reserve(M);
            for (std::size_t i = 0; i < M; ++i)
                all.push_back(Ad::insert(m, make_val<T>(i)));

            if (stride == 1) {
                keys = std::move(all);
            } else {
                keys.reserve(live);
                for (std::size_t i = 0; i < M; ++i) {
                    if (i % stride == 0) keys.push_back(all[i]);
                    else Ad::erase(m, all[i]);
                }
            }
            expected = expected_sum(stride, live);
        }
    };

    // insert: fresh, dynamically-growing map, M = live*stride emplaces,
    // discarded -- the acquire-through-grow path. Reads back the last key so
    // the fill can't be optimised away.
    template <class Ad, class T>
    Result insert(std::size_t live, std::size_t stride) {
        const std::size_t M = live * stride;
        return run(M,
                   [&] {
                       auto m = Ad::template make<T>();
                       typename Ad::template Key<T> last{};
                       for (std::size_t i = 0; i < M; ++i)
                           last = Ad::insert(m, make_val<T>(i));
                       const T* p = Ad::find(m, last);
                       return p ? sum_val(*p) : std::uint64_t{0};
                   });
    }

    // iterate: time a full live-set traversal. Correctness-gated first.
    template <class Ad, class T>
    Result iterate(const char* who, std::size_t live, std::size_t stride) {
        Survivors<Ad, T> f(live, stride);
        check(who,
              f.expected,
              [&] {
                  std::uint64_t sum = 0;
                  Ad::for_each(f.m, [&](const T& v) { sum += sum_val(v); });
                  return sum;
              }());
        return run(live,
                   [&] {
                       std::uint64_t sum = 0;
                       Ad::for_each(f.m,
                                    [&](const T& v) { sum += sum_val(v); });
                       return sum;
                   });
    }

    // find: random-permuted lookups over the live keys (fixed seed).
    template <class Ad, class T>
    Result find(std::size_t live, std::size_t stride) {
        Survivors<Ad, T> f(live, stride);
        std::vector<std::uint32_t> order(live);
        for (std::size_t i = 0; i < live; ++i)
            order[i] = static_cast<std::uint32_t>(i);
        std::mt19937_64 rng(RNG_SEED);
        std::shuffle(order.begin(), order.end(), rng);
        return run(live,
                   [&] {
                       std::uint64_t sum = 0;
                       for (std::size_t i = 0; i < live; ++i)
                           if (const T* p = Ad::find(f.m, f.keys[order[i]]))
                               sum += sum_val(*p);
                       return sum;
                   });
    }

    // churn: balanced erase+reinsert at a fixed live set (no growth). The
    // keys vector is rewritten across reps, staying at `live` throughout.
    template <class Ad, class T>
    Result churn(std::size_t live, std::size_t stride) {
        Survivors<Ad, T> f(live, stride);
        std::mt19937_64 crng(RNG_SEED ^ 0x9E37u);
        return run(live,
                   [&] {
                       std::uint64_t cs = 0;
                       for (std::size_t c = 0; c < live; ++c) {
                           const std::size_t j = crng() % live;
                           Ad::erase(f.m, f.keys[j]);
                           f.keys[j] = Ad::insert(f.m, make_val<T>(c));
                           cs += c;
                       }
                       return cs;
                   });
    }

    // churn_scan: the front-loading stress test. Over-allocate to SPREAD x
    // the live set, thin randomly back down to `live` (leaving survivors
    // scattered across the full high-water), churn in place for a while,
    // then time a sequential scan.
    //
    // A lowest-free finder (HierarchicalBitmap) refills the low holes on
    // every reinsert, so churn *compacts* the live set back toward
    // [0, live) and the scan touches a dense, ~fully-live prefix. A finder
    // that hands back the most-recently-freed slot (LIFO ring / intrusive
    // freelist) never compacts, so the scan keeps paying for the whole
    // SPREAD x-wide, half-dead span -- more pages and TLB entries per live
    // element. The scan ns/elem is the proof; build is not timed.
    template <class Ad, class T>
    Result churn_scan(std::size_t live, std::size_t /*stride*/) {
        constexpr std::size_t SPREAD = 2; // peak = SPREAD * live
        constexpr std::size_t CHURN_MULT = 4;
        // erase+reinsert CHURN_MULT*live
        const std::size_t peak = live * SPREAD;

        auto m = Ad::template make<T>();
        std::vector<typename Ad::template Key<T> > keys;
        keys.reserve(peak);
        for (std::size_t i = 0; i < peak; ++i)
            keys.push_back(Ad::insert(m, make_val<T>(i)));

        std::mt19937_64 rng(RNG_SEED ^ 0x5CA9u);
        // thin to `live`: erase random survivors, swap-remove to keep `keys`
        // dense. Survivors end up scattered across [0, peak).
        while (keys.size() > live) {
            const std::size_t j = rng() % keys.size();
            Ad::erase(m, keys[j]);
            keys[j] = keys.back();
            keys.pop_back();
        }
        // churn in place at a fixed live count. This is where a lowest-free
        // finder compacts and a LIFO one does not.
        for (std::size_t c = 0; c < CHURN_MULT * live; ++c) {
            const std::size_t j = rng() % live;
            Ad::erase(m, keys[j]);
            keys[j] = Ad::insert(m, make_val<T>(c));
        }

        // gate: churn must have stayed balanced at `live`.
        std::size_t count = 0;
        Ad::for_each(m, [&](const T&) { ++count; });
        check("churn_scan count", live, count);

        return run(live,
                   [&] {
                       std::uint64_t sum = 0;
                       Ad::for_each(m,
                                    [&](const T& v) { sum += sum_val(v); });
                       return sum;
                   });
    }
} // namespace phase