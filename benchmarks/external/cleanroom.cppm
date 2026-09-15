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

    auto eq = [](const char* a, const char* b) {
        return std::strcmp(a, b) == 0;
    };
    const Sel s = bench::parse_sel(phase, elem, dens);

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

    bench::print_header("livealloc vs sergey");

    if (eq(who, "both") || eq(who, "livealloc"))
        bench::isolated([&] {
            bench::run_adapter<adapters::IncoLiveAllocAd>(s);
        });
    if (eq(who, "both") || eq(who, "sergey"))
        bench::isolated([&] { bench::run_adapter<adapters::SergeyAd>(s); });

    std::printf("\n[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}