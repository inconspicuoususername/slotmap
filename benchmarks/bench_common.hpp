#pragma once

#include <algorithm>
#include <array>
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

    inline constexpr int WARMUP = 3;
    inline constexpr int REPS = 15;

    // Kept live so nothing gets optimised away.
    inline volatile std::uint64_t g_sink = 0;

    // Evict the caches so a measurement doesn't silently inherit the previous
    // phase's warm working set
    inline void flush_cache() noexcept {
        static constexpr std::size_t BYTES = 64u << 20; // 64 MiB > L3
        static std::vector<std::uint64_t> buf(BYTES / sizeof(std::uint64_t), 1);
        std::uint64_t acc = 0;
        // one write per 64B line dirties the whole buffer, forcing eviction
        for (std::size_t i = 0; i < buf.size(); i += 8) buf[i] += acc + i;
        for (std::size_t i = 0; i < buf.size(); i += 8) acc += buf[i];
        g_sink += acc;
    }

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
        double med_ms = 0; // median rep -- robust central estimate
        double ns_per_elem = 0; // from min
        double med_ns_per_elem = 0;
        double spread_pct = 0; // (max-min)/min*100 -- how noisy the reps were
        std::uint64_t checksum = 0;
        std::size_t elems = 0;
    };

    // Run fn() (returns a checksum) and time it REPS times
    template <class Fn>
    Result run(std::size_t elems, Fn&& fn) {
        using clock = std::chrono::steady_clock;
        std::uint64_t cs = 0;

        flush_cache(); // decouple from prior phase
        for (int i = 0; i < WARMUP; ++i) cs ^= fn();

        std::array<double, REPS> ms{};
        for (int i = 0; i < REPS; ++i) {
            const auto t0 = clock::now();
            cs += fn();
            const auto t1 = clock::now();
            ms[i] = std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        g_sink += cs;

        std::sort(ms.begin(), ms.end());
        const double min_ms = ms.front();
        const double med_ms = ms[REPS / 2];
        const double max_ms = ms.back();

        const double e = static_cast<double>(elems);
        return Result{
            min_ms,
            med_ms,
            elems ? min_ms * 1e6 / e : 0.0,
            elems ? med_ms * 1e6 / e : 0.0,
            min_ms > 0 ? (max_ms - min_ms) / min_ms * 100.0 : 0.0,
            cs,
            elems,
        };
    }

    inline void print_header(const char* section) {
        std::printf("\n== %s ==\n", section);
        std::printf("%-30s %-9s %-8s %10s %10s %10s %8s\n",
                    "impl",
                    "pattern",
                    "elem",
                    "live",
                    "ns/elem",
                    "med_ns",
                    "spread%");
        std::printf("%s\n", std::string(89, '-').c_str());
    }

    inline void print_row(const char* impl, const char* pattern,
                          const char* elem, const Result& r) {
        std::printf("%-30s %-9s %-8s %10zu %10.3f %10.3f %7.1f%%\n",
                    impl,
                    pattern,
                    elem,
                    r.elems,
                    r.ns_per_elem,
                    r.med_ns_per_elem,

                    r.spread_pct);
        std::fflush(stdout);
    }

    // Fixed seed so the random find/churn drivers hit the same slots every run.
    inline constexpr std::uint64_t RNG_SEED = 0x5EED'1234'ABCDu;

    // Survivors are {0, s, 2s, ..., (live-1)s}, inserted with value id == their
    // index, so the live id-sum is s * (0+1+...+(live-1)). Any re-read or skip
    // in an iteration fails this.
    inline std::uint64_t
    expected_sum(std::size_t stride, std::size_t live = N) {
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
                    Ad::name,
                    p.name,
                    elem,
                    M,
                    Ad::max_slots);
                std::fflush(stdout);
                continue;
            }

            // ---- insert: fresh, dynamically-growing map, M emplaces, discard.
            // Sink reads back the last inserted element so the fill can't be
            // optimised away and the value it returns depends on real storage.
            print_row(
                "insert",
                p.name,
                elem,
                run(M,
                    [&] {
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
            check(Ad::name,
                  want,
                  [&] {
                      std::uint64_t sum = 0;
                      Ad::for_each(m, [&](const T& v) { sum += sum_val(v); });
                      return sum;
                  }());
            print_row(
                "iterate",
                p.name,
                elem,
                run(live,
                    [&] {
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
                "find",
                p.name,
                elem,
                run(live,
                    [&] {
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
                "churn",
                p.name,
                elem,
                run(live,
                    [&] {
                        std::uint64_t cs = 0;
                        for (std::size_t c = 0; c < live; ++c) {
                            const std::size_t j = crng() % live;
                            Ad::erase(m, live_keys[j]);
                            live_keys[j] = Ad::insert(m, make_val<T>(c));
                            cs += c;
                        }
                        return cs;
                    }));


            // TODO churn and iterate
        }
    }
}