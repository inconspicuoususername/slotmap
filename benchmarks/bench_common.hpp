// Shared benchmark harness. Header-only, no module/std-import so it can be pulled
// into both the module TU (in its GMF) and the plain .cpp reference TU.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

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
}