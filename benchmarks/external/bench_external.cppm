module;

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "bench_common.hpp"

export module bench_external;

import slotmap;

using bench::Payload64;
using bench::run_lifecycle_elem;

namespace {
    struct IncoAd {
        static constexpr const char* name =
            "inconspicuoususername (SparseSlotMap)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = slotmap::SparseSlotMap<T>;
        template <class T>
        using Key = typename slotmap::SparseSlotMap<T>::key_type;

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
}

// Implemented in ext_refs.cpp (plain TU, no modules).
extern "C" void bench_external_sporacid();

extern "C" void bench_external_sergey();

extern "C" void bench_external_twiggler();

extern "C++" int main() {
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

    section<IncoAd>(
        "inconspicuoususername (SparseSlotMap)  -- dynamically sized");

    // sporacid is fixed-capacity (sized at compile time); the others grow.
    bench::print_header("sporacid (bitmap)  -- fixed capacity");
    bench_external_sporacid();

    bench::print_header("SergeyMakeev (paged)  -- dynamically sized");
    bench_external_sergey();

    // twiggler is capped at 65535 slots on GCC 16 (evolve() build bug), so it
    // runs a reduced workload -- NOT comparable to the 512k rows above.
    bench::print_header(
        "twiggler (skipfield)  -- reduced N=32768 (capped at 65535 slots)");
    bench_external_twiggler();

    std::printf("\n[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}