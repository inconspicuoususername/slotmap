#pragma once
#include <unistd.h>
#include <sys/wait.h>

#include <chrono>
#include <string>
#include <vector>

namespace bench {
    // Fixed live-set size for every measurement so cross-impl numbers line up.
    // 512K elements -> 32MB of Payload64 live at 100% density, well past L3.
    inline constexpr std::size_t N = 1u << 20;

    inline constexpr int WARMUP = 3;
    inline constexpr int REPS = 15;

    struct Result {
        double min_ms = 0;
        double med_ms = 0; // median rep -- robust central estimate
        double ns_per_elem = 0; // from min
        double med_ns_per_elem = 0;
        double spread_pct = 0; // (max-min)/min*100 -- how noisy the reps were
        std::uint64_t checksum = 0;
        std::size_t elems = 0;
    };

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

    template <class F>
    void isolated(F&& body) {
        std::fflush(stdout); // don't let the child re-print the parent's buffer
        const pid_t pid = fork();
        if (pid == 0) {
            std::forward<F>(body)();
            std::fflush(stdout);
            _exit(0); // skip atexit/global dtors -- avoid double flush/free
        }
        int status = 0;
        waitpid(pid, &status, 0);
        bench::flush_cache(); // evict this child's warm lines before the next
    }

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
            .min_ms = min_ms,
            .med_ms = med_ms,
            .ns_per_elem = elems ? min_ms * 1e6 / e : 0.0,
            .med_ns_per_elem = elems ? med_ms * 1e6 / e : 0.0,
            .spread_pct = min_ms > 0 ? (max_ms - min_ms) / min_ms * 100.0 : 0.0,
            .checksum = cs,
            .elems = elems,
        };
    }
}