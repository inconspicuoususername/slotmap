module;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

#include "bench_common.hpp"

export module bench_external;

import slotmap;

using bench::Payload64;
using bench::run_lifecycle_elem;

namespace {
    struct IncoAd {
        static constexpr const char* name =
            "inconspicuoususername (HierarchichalBitmap)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = inco::SparseSlotMap<T, inco::FreeList>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) {
            return m.try_emplace(v).value();
        }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            for (auto&& e : m) f(e.value);
        }
    };

    struct IncoLiveAllocAd {
        static constexpr const char* name =
            "inconspicuoususername (LiveAllocBitmap)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = inco::SparseSlotMap<T, T, inco::LiveAllocBitmap>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) {
            return m.emplace(v);
        }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            for (auto&& e : m) f(e.value);
        }
    };

    struct IncoSoAAd {
        static constexpr const char* name =
            "inconspicuoususername (LiveAlloc+SoA)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = inco::SparseSlotMap<T, T, inco::LiveAllocBitmap,
                                        inco::SoAStore<T>>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) {
            return m.try_emplace(v).value();
        }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            for (auto&& e : m) f(e.value);
        }
    };

    template <class Ad>
    void section(const char* title) {
        bench::print_header(title);
        run_lifecycle_elem<Ad, std::uint32_t>("u32");
        run_lifecycle_elem<Ad, Payload64>("P64");
    }

    // Run one section in its own child process so it can't inherit the heap the
    // previous sections grew -- arena size, mmap layout, THP backing and the
    // physical page placement that drives L3 set-conflict misses all reset to a
    // fresh process. Without this, whoever runs last gets a warmed environment
    // the earlier sections paid to create (Sergey's 11.7-vs-18 churn was exactly
    // this order bias). waitpid serializes the children so output stays ordered;
    // the parent flushes the caches between them since the CPU cache survives the
    // process switch even though the heap doesn't.
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
}

// Implemented in ext_refs.cpp (plain TU, no modules).
extern "C" void bench_external_sporacid();

extern "C" void bench_external_sergey();

extern "C" void bench_external_twiggler();

extern "C++" int main(int argc, char** argv) {
    // Optional argv[1] filter: run only the sections whose name contains it
    // (e.g. "sergey", "livealloc"). Lets a single impl be isolated for perf.
    const char* only = argc > 1 ? argv[1] : nullptr;
    auto want = [&](const char* n) {
        if (!only) return true;
        for (const char* h = n; *h; ++h) {
            const char* a = h;
            const char* b = only;
            while (*a && *b && *a == *b) { ++a; ++b; }
            if (!*b) return true;
        }
        return false;
    };

    std::printf(
        "slotmap external benchmark  (N=%zu live, warmup=%d reps=%d)\n",
        bench::N,
        bench::WARMUP,
        bench::REPS);
    std::printf(
        "sizeof(Payload64)=%zu  min wall time over reps; ns per element\n"
        "ops: insert (build w/ dynamic growth) | iterate (live) | "
        "find (N random) | churn (N erase+reinsert)\n",
        sizeof(Payload64));

    if (want("sergey"))
        isolated([] {
            bench::print_header("SergeyMakeev (paged)  -- dynamically sized");
            bench_external_sergey();
        });

    if (want("sparse"))
        isolated([] {
            section<IncoAd>(
                "inconspicuoususername (UnboundedDeferBitmap)  -- dynamically sized");
        });

    if (want("livealloc"))
        isolated([] {
            section<IncoLiveAllocAd>(
                "inconspicuoususername (LiveAllocBitmap)  -- dynamically sized");
        });

    if (want("soa"))
        isolated([] {
            section<IncoSoAAd>(
                "inconspicuoususername (LiveAlloc+SoA inline-meta)  -- "
                "dynamically sized");
        });

    // sporacid is fixed-capacity (sized at compile time); the others grow.
    if (want("sporacid"))
        isolated([] {
            bench::print_header("sporacid (bitmap)  -- fixed capacity");
            bench_external_sporacid();
        });

    // twiggler is capped at 65535 slots on GCC 16 (evolve() build bug), so it
    // runs a reduced workload -- NOT comparable to the 512k rows above.
    if (want("twiggler"))
        isolated([] {
            bench::print_header(
                "twiggler (skipfield)  -- reduced N=32768 (capped at 65535 "
                "slots)");
            bench_external_twiggler();
        });

    std::printf("\n[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}