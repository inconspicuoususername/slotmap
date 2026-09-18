module;
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "matrix.hpp"

export module cleanroom;

import slotmap;
import adapters;

using bench::Payload64;
using bench::Sel;

extern "C++" int main(int argc, char** argv) {
    const char* who = argc > 1 ? argv[1] : "both";
    const char* phase = argc > 2 ? argv[2] : "insert";
    const char* elem = argc > 3 ? argv[3] : "p64";
    const char* dens = argc > 4 ? argv[4] : "packed";
    const char* shutup = argc > 5 ? argv[5] : "";

    auto eq = [](const char* a, const char* b) {
        return std::strcmp(a, b) == 0;
    };
    const Sel s = bench::parse_sel(phase, elem, dens);

    if (!eq(shutup, "shutup")) {
        std::printf(
            "clean-room A/B  N=%zu  warmup=%d reps=%d  sizeof(P64)=%zu\n",
            bench::N,
            bench::WARMUP,
            bench::REPS,
            sizeof(Payload64));
        std::printf(
            "who=%s phase=%s elem=%s dens=%s  "
            "(narrow to one row for perf)\n",
            who,
            phase,
            elem,
            dens);
    }

    if (eq(who, "both") || eq(who, "livealloc"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoLiveAllocAd>(s);
        });

    if (eq(who, "both") || eq(who, "liveallocsoa"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoSoAAd>(s);
        });

    if (eq(who, "both") || eq(who, "prefetchiter"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoPrefetchIterAd>(s);
        });

    if (eq(who, "both") || eq(who, "prefetchfn"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoPrefetchFnAd>(s);
        });

    if (eq(who, "both") || eq(who, "walkfn"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoWalkFnAd>(s);
        });

    if (eq(who, "both") || eq(who, "expandfn"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoExpandFnAd>(s);
        });

    if (eq(who, "both") || eq(who, "unrollfn"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoUnrollFnAd>(s);
        });

    if (eq(who, "both") || eq(who, "avxfn"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoAVXFnAd>(s);
        });

    if (eq(who, "both") || eq(who, "avxlanesfn"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoAVXLanesFnAd>(s);
        });

    if (eq(who, "both") || eq(who, "unroll"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoUnrollIteratorAd>(s);
        });

    if (eq(who, "both") || eq(who, "sergey"))
        bench::isolated([&] { bench::run_adapter<adapters::SergeyAd>(s); });

    if (eq(who, "both") || eq(who, "sporacid"))
        bench::isolated([&] { bench::run_adapter<adapters::SporacidAd>(s); });

    if (!eq(shutup, "shutup")) {
        std::printf("\n[sink=%llu]\n",
                    static_cast<unsigned long long>(bench::g_sink));
    }
    return 0;
}