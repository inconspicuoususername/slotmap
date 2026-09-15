#pragma once
#include "phase.hpp"

namespace bench {
    struct Pattern {
        const char* name;
        std::size_t stride;
    };

    // packed = every slot live; half = every other slot live (survivors spread
    // over a 2x-wide high-water so iteration crosses dead slots).
    inline constexpr Pattern LIFECYCLE_PATTERNS[] = {
        {"packed", 1}, {"half", 2},
    };

    enum class Op { insert, iterate, find, churn, churn_scan };

    inline const char* op_name(Op o) {
        switch (o) {
            case Op::insert:
                return "insert";
            case Op::iterate:
                return "iterate";
            case Op::find:
                return "find";
            case Op::churn:
                return "churn";
            case Op::churn_scan:
                return "churnscan";
        }
        return "?";
    }

    // Run one (op, elem, density) for adapter Ad and print one row. `label` is
    // the impl column -- "insert" in the lifecycle view, "<name>:insert" in the
    // A/B view. Skips (with a note) when the high-water M = live*stride would
    // exceed the adapter's capacity.
    template <class Ad, class T>
    bool run_op(Op o, const char* label, const char* elem, const char* dens,
                std::size_t live, std::size_t stride) {
        const std::size_t M = live * stride;
        if (M > Ad::max_slots) {
            std::printf("%-30s %-9s %-8s   skipped: M=%zu exceeds cap %zu\n",
                        label,
                        dens,
                        elem,
                        M,
                        Ad::max_slots);
            std::fflush(stdout);
            return false;
        }
        Result r;
        switch (o) {
            case Op::insert:
                r = phase::insert<Ad, T>(live, stride);
                break;
            case Op::iterate:
                r = phase::iterate<Ad, T>(label, live, stride);
                break;
            case Op::find:
                r = phase::find<Ad, T>(live, stride);
                break;
            case Op::churn:
                r = phase::churn<Ad, T>(live, stride);
                break;
            case Op::churn_scan:
                r = phase::churn_scan<Ad, T>(live, stride);
                break;
        }
        print_row(label, dens, elem, r);
        return true;
    }

    // Cross-library lifecycle driver: for each density, time the four things a
    // slotmap is actually used for -- build, live iteration, random lookup,
    // erase+reinsert churn -- on the identical value set. Now pure assembly over
    // the phase primitives; rows are labelled by op (the section header names
    // the adapter). A pattern whose high-water M = live*stride exceeds the
    // adapter's Ad::max_slots is skipped with a note by run_op.
    template <class Ad, class T>
    void run_lifecycle_elem(const char* elem, std::size_t live = N) {
        for (const auto& p : LIFECYCLE_PATTERNS) {
            run_op<Ad, T>(Op::insert, "insert", elem, p.name, live, p.stride);
            run_op<Ad, T>(Op::iterate, "iterate", elem, p.name, live, p.stride);
            run_op<Ad, T>(Op::find, "find", elem, p.name, live, p.stride);
            run_op<Ad, T>(Op::churn, "churn", elem, p.name, live, p.stride);
            run_op<Ad, T>(Op::churn_scan,
                          "churn&iterate",
                          elem,
                          p.name,
                          live,
                          p.stride);
        }
    }

    template <class Ad>
    void run_lib(std::size_t live) {
        bench::run_lifecycle_elem<Ad, std::uint32_t>("u32", live);
        bench::run_lifecycle_elem<Ad, bench::Payload64>("P64", live);
    }
}