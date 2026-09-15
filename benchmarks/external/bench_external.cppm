module;

#include <cstdint>
#include <cstdio>

#include "common.hpp"
#include "op.hpp"

export module bench_external;

import slotmap;
import adapters;

using bench::Payload64;
using bench::run_lifecycle_elem;

template <class Ad>
void section() {
    bench::print_header(Ad::name);
    run_lifecycle_elem<Ad, std::uint32_t>("u32");
    run_lifecycle_elem<Ad, Payload64>("P64");
}

extern "C++" int main(int argc, char** argv) {
    // Optional argv[1] filter: run only the sections whose name contains it
    // (e.g. "sergey", "livealloc"). Lets a single impl be isolated for perf.
    const char* only = argc > 1 ? argv[1] : nullptr;
    auto want = [&](const char* n) {
        if (!only) return true;
        for (const char* h = n; *h; ++h) {
            const char* a = h;
            const char* b = only;
            while (*a && *b && *a == *b) {
                ++a;
                ++b;
            }
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
        "sizeof(Payload64)=%zu  min wall time over reps; ns per element\n",
        sizeof(Payload64));

    if (want("sergey"))
        bench::isolated([] {
            bench::print_header("SergeyMakeev (paged)  -- dynamically sized");
            bench::run_lib<adapters::SergeyAd>(bench::N);
        });

    if (want("sparse"))
        bench::isolated([] {
            section<adapters::IncoAd>();
        });

    if (want("livealloc"))
        bench::isolated([] {
            section<adapters::IncoLiveAllocAd>();
        });

    if (want("soa"))
        bench::isolated([] {
            section<adapters::IncoSoAAd>();
        });

    // sporacid is fixed-capacity (sized at compile time); the others grow.
    if (want("sporacid"))
        bench::isolated([] {
            bench::print_header("sporacid (bitmap)  -- fixed capacity");
            bench::run_lib<adapters::SporacidAd>(bench::N);
        });

    // twiggler is capped at 65535 slots on GCC 16 (evolve() build bug), so it
    // // runs a reduced workload -- NOT comparable to the 512k rows above.
    // if (want("twiggler"))
    //     bench::isolated([] {
    //         bench::print_header(
    //             "twiggler (skipfield)  -- reduced N=32768 (capped at 65535 "
    //             "slots)");
    //         bench::run_lib<adapters::TwigAd>(1u << 15);
    //     });

    std::printf("\n[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}