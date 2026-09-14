// Shared benchmark harness. Header-only, no module/std-import so it can be pulled
// into both the module TU (in its GMF) and the plain .cpp reference TU.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace bench {
    // Fixed live-set size for every measurement so cross-impl numbers line up.
    // 512K elements -> 32MB of Payload64 live at 100% density, well past L3.
    inline constexpr std::size_t N = 1u << 19;

    inline constexpr int WARMUP = 2;
    inline constexpr int REPS = 7;

    // Kept live so nothing gets optimised away.
    inline volatile std::uint64_t g_sink = 0;

    // 64B == one cache line. The "gather is a cache miss per element" case.
    struct Payload64 {
        std::uint64_t id;
        std::uint64_t rest[7];
    };

    static_assert(sizeof(Payload64) == 64);

    // value factories / field extractors, shared by every impl's fill+sum loop
    template <class T>
    T make_val(std::uint64_t i);

    template <>
    inline std::uint32_t make_val<std::uint32_t>(std::uint64_t i) {
        return static_cast<std::uint32_t>(i);
    }

    template <>
    inline Payload64 make_val<Payload64>(std::uint64_t i) {
        Payload64 p{};
        p.id = i;
        return p;
    }

    inline std::uint64_t sum_val(std::uint32_t v) noexcept { return v; }
    inline std::uint64_t sum_val(const Payload64& v) noexcept { return v.id; }

    struct Result {
        double min_ms = 0;
        double ns_per_elem = 0;
        std::uint64_t checksum = 0;
        std::size_t elems = 0;
    };

    // Run fn() (returns a checksum) WARMUP+REPS times, keep the fastest wall time.
    // Min, not mean: we want the ceiling the machine can hit, with noise cut out.
    template <class Fn>
    Result run(std::size_t elems, Fn&& fn) {
        using clock = std::chrono::steady_clock;
        std::uint64_t cs = 0;
        for (int i = 0; i < WARMUP; ++i) cs ^= fn();

        double best_ms = 1e300;
        for (int i = 0; i < REPS; ++i) {
            const auto t0 = clock::now();
            cs += fn();
            const auto t1 = clock::now();
            const double ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            best_ms = std::min(best_ms, ms);
        }
        g_sink += cs;

        const double ns_per =
            elems ? best_ms * 1e6 / static_cast<double>(elems) : 0.0;
        return Result{best_ms, ns_per, cs, elems};
    }

    inline void print_header(const char* section) {
        std::printf("\n== %s ==\n", section);
        std::printf("%-30s %-9s %-8s %10s %11s %10s\n",
                    "impl",
                    "pattern",
                    "elem",
                    "live",
                    "min_ms",
                    "ns/elem");
        std::printf("%s\n", std::string(82, '-').c_str());
    }

    inline void print_row(const char* impl, const char* pattern,
                          const char* elem, const Result& r) {
        std::printf("%-30s %-9s %-8s %10zu %11.3f %10.3f\n",
                    impl,
                    pattern,
                    elem,
                    r.elems,
                    r.min_ms,
                    r.ns_per_elem);
        std::fflush(stdout);
    }

    // Fixed seed so the random find/churn drivers hit the same slots every run.
    inline constexpr std::uint64_t RNG_SEED = 0x5EED'1234'ABCDu;

    // Survivors are {0, s, 2s, ..., (live-1)s}, inserted with value id == their
    // index, so the live id-sum is s * (0+1+...+(live-1)). Any re-read or skip
    // in an iteration fails this.
    inline std::uint64_t expected_sum(std::size_t stride, std::size_t live = N) {
        const std::uint64_t n = live;
        return static_cast<std::uint64_t>(stride) * (n * (n - 1) / 2);
    }

    inline void check(const char* name, std::uint64_t want, std::uint64_t got) {
        if (want != got)
            std::printf("  !! CHECK FAILED %-26s want=%llu got=%llu\n",
                        name,
                        static_cast<unsigned long long>(want),
                        static_cast<unsigned long long>(got));
    }

    struct Pattern {
        const char* name;
        std::size_t stride;
    };

    // packed = every slot live; half = every other slot live (survivors spread
    // over a 2x-wide high-water so iteration crosses dead slots).
    inline constexpr Pattern LIFECYCLE_PATTERNS[] = {
        {"packed", 1}, {"half", 2},
    };

    // Cross-library lifecycle driver. `Ad` is a thin adapter exposing one map
    // shape (see the adapters in the external bench TUs). For each density it
    // times the four things a slotmap is actually used for -- build, random
    // lookup, erase+reinsert churn, and live iteration -- against the identical
    // value set, so every library is doing the same work. `live` is the survivor
    // count; a pattern whose high-water M = live*stride exceeds the adapter's
    // Ad::max_slots is skipped with a note instead of overrunning the map.
    template <class Ad, class T>
    void run_lifecycle_elem(const char* elem, std::size_t live = N) {
        for (const auto& p : LIFECYCLE_PATTERNS) {
            const std::size_t s = p.stride;
            const std::size_t M = live * s; // total inserted before thinning
            using Map = typename Ad::template Map<T>;
            using Key = typename Ad::template Key<T>;

            if (M > Ad::max_slots) {
                std::printf(
                    "%-30s %-9s %-8s   skipped: M=%zu exceeds cap %zu\n",
                    Ad::name, p.name, elem, M, Ad::max_slots);
                std::fflush(stdout);
                continue;
            }

            // ---- insert: fresh, dynamically-growing map, M emplaces, discard.
            // Sink reads back the last inserted element so the fill can't be
            // optimised away and the value it returns depends on real storage.
            print_row(
                "insert", p.name, elem,
                run(M, [&] {
                    Map m = Ad::template make<T>();
                    Key last = Ad::insert(m, make_val<T>(0));
                    for (std::size_t i = 1; i < M; ++i)
                        last = Ad::insert(m, make_val<T>(i));
                    const T* p = Ad::find(m, last);
                    return p ? sum_val(*p) : 0;
                }));

            // ---- persistent map for the read/mutate/iterate phases.
            Map m = Ad::template make<T>();
            std::vector<Key> all;
            all.reserve(M);
            for (std::size_t i = 0; i < M; ++i)
                all.push_back(Ad::insert(m, make_val<T>(i)));

            // thin to the survivor set: keep i % s == 0, erase the rest.
            std::vector<Key> live_keys;
            live_keys.reserve(live);
            for (std::size_t i = 0; i < M; ++i) {
                if (i % s == 0) live_keys.push_back(all[i]);
                else Ad::erase(m, all[i]);
            }

            const std::uint64_t want = expected_sum(s, live);

            // ---- iterate (correctness gated, then timed).
            check(Ad::name, want, [&] {
                std::uint64_t sum = 0;
                Ad::for_each(m, [&](const T& v) { sum += sum_val(v); });
                return sum;
            }());
            print_row(
                "iterate", p.name, elem,
                run(live, [&] {
                    std::uint64_t sum = 0;
                    Ad::for_each(m, [&](const T& v) { sum += sum_val(v); });
                    return sum;
                }));

            // ---- random find over the live keys (fixed permutation).
            std::vector<std::uint32_t> order(live);
            for (std::size_t i = 0; i < live; ++i)
                order[i] = static_cast<std::uint32_t>(i);
            std::mt19937_64 rng(RNG_SEED);
            std::shuffle(order.begin(), order.end(), rng);

            print_row(
                "find", p.name, elem,
                run(live, [&] {
                    std::uint64_t sum = 0;
                    for (std::size_t i = 0; i < live; ++i)
                        if (const T* p = Ad::find(m, live_keys[order[i]]))
                            sum += sum_val(*p);
                    return sum;
                }));

            // ---- churn: erase a live key, reinsert, live times. Balanced, so
            // the map stays at `live` across reps. Runs last: it rewrites values.
            std::mt19937_64 crng(RNG_SEED ^ 0x9E37u);
            print_row(
                "churn", p.name, elem,
                run(live, [&] {
                    std::uint64_t cs = 0;
                    for (std::size_t c = 0; c < live; ++c) {
                        const std::size_t j = crng() % live;
                        Ad::erase(m, live_keys[j]);
                        live_keys[j] = Ad::insert(m, make_val<T>(c));
                        cs += c;
                    }
                    return cs;
                }));
        }
    }
}