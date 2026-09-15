#pragma once
#include "common.hpp"
#include "op.hpp"

namespace bench {
    struct Sel {
        bool insert = true, iterate = false, find = false, churn = false;
        bool churn_scan = false;
        bool u32 = false, p64 = true;
        bool packed = true, half = false;
        std::size_t live = N;
    };

    // Positional CLI shared by the perf drivers: <phase> <elem> <dens>.
    //   phase = all | insert | iterate | find | churn
    //   elem  = both | u32 | p64
    //   dens  = both | packed | half
    inline Sel parse_sel(const char* phase, const char* elem,
                         const char* dens) {
        auto eq = [](const char* a, const char* b) {
            return std::strcmp(a, b) == 0;
        };
        Sel s;
        s.insert = eq(phase, "all") || eq(phase, "insert");
        s.iterate = eq(phase, "all") || eq(phase, "iterate");
        s.find = eq(phase, "all") || eq(phase, "find");
        s.churn = eq(phase, "all") || eq(phase, "churn");
        // Specialised stress test -- explicit only, not part of "all".
        s.churn_scan = eq(phase, "churn_scan") || eq(phase, "scan");
        s.u32 = eq(elem, "both") || eq(elem, "u32");
        s.p64 = eq(elem, "both") || eq(elem, "p64") || eq(elem, "P64");
        s.packed = eq(dens, "both") || eq(dens, "packed");
        s.half = eq(dens, "both") || eq(dens, "half");
        return s;
    }

    template <class Ad, class T>
    void run_elem(const Sel& s, const char* elem) {
        const struct {
            const char* name;
            std::size_t stride;
            bool on;
        }
        densities[] = {
            {"packed", 1, s.packed},
            {"half", 2, s.half},
        };
        const std::string base = Ad::name;
        for (const auto& d : densities) {
            if (!d.on) continue;
            const std::string i = base + ":insert";
            const std::string it = base + ":iterate";
            const std::string fd = base + ":find";
            const std::string ch = base + ":churn";
            const std::string cs = base + ":churnscan";
            if (s.insert)
                run_op<Ad, T>(Op::insert,
                              i.c_str(),
                              elem,
                              d.name,
                              s.live,
                              d.stride);
            if (s.iterate)
                run_op<Ad, T>(Op::iterate,
                              it.c_str(),
                              elem,
                              d.name,
                              s.live,
                              d.stride);
            if (s.find)
                run_op<Ad, T>(Op::find,
                              fd.c_str(),
                              elem,
                              d.name,
                              s.live,
                              d.stride);
            if (s.churn)
                run_op<Ad, T>(Op::churn,
                              ch.c_str(),
                              elem,
                              d.name,
                              s.live,
                              d.stride);
            if (s.churn_scan)
                run_op<Ad, T>(Op::churn_scan,
                              cs.c_str(),
                              elem,
                              d.name,
                              s.live,
                              d.stride);
        }
    }

    template <class Ad>
    void run_adapter(const Sel& s) {
        if (s.u32) run_elem<Ad, std::uint32_t>(s, "u32");
        if (s.p64) run_elem<Ad, Payload64>(s, "P64");
    }
}