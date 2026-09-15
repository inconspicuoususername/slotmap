// Isolated insert / churn / iterate / find microbench for the internal free
// finder x storage matrix. The external bench runs every phase in one process,
// so `perf stat` counters can't be attributed to a single operation; this
// binary runs exactly the (phase x elem x density x variant) subset you ask for
// so the counters describe just that.
//
// It is pure assembly over the shared phase primitives in bench_common.hpp --
// the same code the clean-room A/B and the external lifecycle use -- so numbers
// here line up row-for-row with those. Each (finder x storage) pair is one
// adapter; a single build runs all four and the `impl` column reads
// "<finder>.<storage>:<op>".
//
//   ./slotmap-perf insert p64 packed livealloc.split
//   ./slotmap-perf churn  both both  livealloc
//   ./slotmap-perf                    # all phases, all variants
module;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "macros.hpp"
#include "common.hpp"
#include "matrix.hpp"

export module perf_churn;

import slotmap;

using bench::N;
using bench::Sel;

namespace {
    PERF_ADAPTER(OffSplitAd,
                 "off.split",
                 inco::HierarchicalBitmap,
                 inco::SplitStore);

    PERF_ADAPTER(OffSoAAd, "off.soa", inco::HierarchicalBitmap, inco::SoAStore);

    PERF_ADAPTER(LiveAllocSplitAd,
                 "livealloc.split",
                 inco::LiveAllocBitmap,
                 inco::SplitStore);

    PERF_ADAPTER(LiveAllocSoAAd,
                 "livealloc.soa",
                 inco::LiveAllocBitmap,
                 inco::SoAStore);

#undef PERF_ADAPTER
}

extern "C++" int main(int argc, char** argv) {
    const char* phase = argc > 1 ? argv[1] : "all";
    const char* elem = argc > 2 ? argv[2] : "both";
    const char* dens = argc > 3 ? argv[3] : "both";
    // 4th arg filters the (finder x storage) variants so perf-stat counters
    // aren't mixed across instantiations. Matches a full name ("livealloc.soa"),
    // a finder ("off"/"livealloc"), or a storage ("split"/"soa"). Default all.
    const char* sel = argc > 4 ? argv[4] : "all";

    const Sel s = bench::parse_sel(phase, elem, dens);

    // run this variant if `sel` is "all", or equals its full name / finder /
    // storage tag.
    auto want = [&](const char* full, const char* finder, const char* storage) {
        auto eq = [](const char* a, const char* b) {
            return std::strcmp(a, b) == 0;
        };
        return eq(sel, "all") || eq(sel, full) || eq(sel, finder) || eq(
                   sel,
                   storage);
    };

    std::printf("perf_churn  N=%zu  (min of %d reps)\n", N, bench::REPS);
    bench::print_header("insert / churn / iterate / find (finder x storage)");

    if (want("off.split", "off", "split")) bench::run_adapter<OffSplitAd>(s);
    if (want("off.soa", "off", "soa")) bench::run_adapter<OffSoAAd>(s);
    if (want("livealloc.split", "livealloc", "split"))
        bench::run_adapter<LiveAllocSplitAd>(s);
    if (want("livealloc.soa", "livealloc", "soa"))
        bench::run_adapter<LiveAllocSoAAd>(s);

    std::printf("[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}