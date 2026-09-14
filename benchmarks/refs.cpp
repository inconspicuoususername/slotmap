// Cross-library comparison. Plain TU (no modules) so the header-only reference
// slotmaps can be pulled in normally. twiggler is intentionally excluded: it
// depends on Boost (boost::uint_t, boost::iterator_adaptor), which isn't wired
// into this standalone build.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "bench_common.hpp"

#include <spore/slot_map.hpp>   // sporacid  -- hierarchical-bitmap, same family as ours
#include <slot_map.h>           // SergeyMakeev -- page-based free-index reuse

using bench::Payload64;
using bench::make_val;
using bench::sum_val;

namespace {
    // sporacid is compile-time sized. CAP must cover the highest high-water we
    // build here (half density -> 2N == 1<<20) with headroom -- filling it to
    // exactly capacity trips its full-map edge. Quarter is left to "ours".
    constexpr std::size_t SPORE_CAP = 1u << 21;

    template <class T>
    void bench_sporacid(const char* elem, const char* pat, std::size_t stride) {
        using Map = spore::slot_map_st<spore::slot_key, T, SPORE_CAP>;
        Map m;
        const std::size_t M = bench::N * stride;
        std::vector<spore::slot_key> keys;
        keys.reserve(M);
        for (std::size_t i = 0; i < M; ++i)
            keys.push_back(m.emplace(make_val<T>(i)));
        if (stride > 1)
            for (std::size_t i = 0; i < M; ++i)
                if (i % stride != 0) m.erase(keys[i]);

        bench::print_row(
            "sporacid (bitmap)", pat, elem,
            bench::run(bench::N, [&] {
                std::uint64_t s = 0;
                for (auto&& [k, v] : m) s += sum_val(v);
                return s;
            }));
    }

    template <class T>
    void bench_sergey(const char* elem, const char* pat, std::size_t stride) {
        using Map = dod::slot_map<T>;
        Map m;
        const std::size_t M = bench::N * stride;
        std::vector<typename Map::key> keys;
        keys.reserve(M);
        for (std::size_t i = 0; i < M; ++i)
            keys.push_back(m.emplace(make_val<T>(i)));
        if (stride > 1)
            for (std::size_t i = 0; i < M; ++i)
                if (i % stride != 0) m.erase(keys[i]);

        bench::print_row(
            "SergeyMakeev (paged)", pat, elem,
            bench::run(bench::N, [&] {
                std::uint64_t s = 0;
                for (auto&& v : m) s += sum_val(v);
                return s;
            }));
    }

    template <class T>
    void run_elem(const char* elem) {
        bench_sporacid<T>(elem, "packed", 1);
        bench_sergey<T>(elem, "packed", 1);
        bench_sporacid<T>(elem, "half", 2);
        bench_sergey<T>(elem, "half", 2);
    }
}

extern "C" void bench_refs() {
    bench::print_header(
        "cross-library (compare vs 'SparseSlotMap range-for' rows above)");
    run_elem<std::uint32_t>("u32");
    run_elem<Payload64>("P64");
}
