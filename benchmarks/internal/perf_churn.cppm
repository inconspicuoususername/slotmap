// Isolated insert / churn microbench for the bitmap free-finder family.
//
// The full external bench runs every phase in one process, so `perf stat`
// counters can't be attributed to a single operation. This binary does exactly
// ONE phase (pick via argv) so IPC / stall / cache counters describe just that
// phase. It also prints the same min-of-reps wall time as the other benches.
//
// Purpose: A/B the four free finders against each other. They are plain types
// now (no build flag), so a single build runs all of them and the `impl` column
// reads "<finder>:<op>". Each finder's checksums must match every other's --
// that's the correctness gate across the whole family.
//
//   ./slotmap-perf insert         # insert only, both elem types, all finders
//   ./slotmap-perf churn p64 packed
//   ./slotmap-perf                # all phases
module;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "bench_common.hpp"

export module perf_churn;

import slotmap;

using bench::make_val;
using bench::N;
using bench::Payload64;
using bench::print_row;
using bench::RNG_SEED;
using bench::run;
using bench::sum_val;

namespace {
    // "<finder>:<op>" so paired rows across finders line up in the impl column.
    std::string label(const char* fin, const char* op) {
        return std::string(fin) + ':' + op;
    }

    // Fresh, dynamically-growing map, M emplaces, discard. No reserve() -- this
    // is the same acquire-through-grow path the external insert row measures.
    template <template <class> class Map, class T>
    void insert_phase(const char* fin, const char* elem, std::size_t M) {
        print_row(label(fin, "insert").c_str(),
                  "packed",
                  elem,
                  run(M,
                      [&] {
                          Map<T> m;
                          typename Map<T>::key_type last{};
                          for (std::size_t i = 0; i < M; ++i)
                              last = m.emplace(make_val<T>(i));
                          const T* p = m.find(last);
                          return p ? sum_val(*p) : std::uint64_t{0};
                      }));
    }

    // Live-set iteration (SparseSlotMap range-for -> PageWalkIter). Guards the
    // headline iterate path against a finder's live-plane view being wrong.
    template <template <class> class Map, class T>
    void iterate_phase(const char* fin, const char* elem, const char* dens,
                       std::size_t live, std::size_t stride) {
        const std::size_t M = live * stride;
        using Key = typename Map<T>::key_type;
        Map<T> m;
        m.reserve(M);
        std::vector<Key> keys;
        keys.reserve(M);
        for (std::size_t i = 0; i < M; ++i)
            keys.push_back(m.emplace(make_val<T>(i)));
        if (stride > 1)
            for (std::size_t i = 0; i < M; ++i)
                if (i % stride != 0) m.erase(keys[i]);

        // correctness gate: the live ids are {0, s, 2s, ...}, so their sum is
        // fixed. A finder handing out wrong slots (double-alloc, desynced live
        // plane) fails this before we trust its timing.
        bench::check(label(fin, "iter").c_str(),
                     bench::expected_sum(stride, live),
                     [&] {
                         std::uint64_t sum = 0;
                         for (auto&& e : m) sum += sum_val(e.value);
                         return sum;
                     }());

        print_row(label(fin, "iterate").c_str(),
                  dens,
                  elem,
                  run(live,
                      [&] {
                          std::uint64_t sum = 0;
                          for (auto&& e : m) sum += sum_val(e.value);
                          return sum;
                      }));
    }

    // Balanced erase+reinsert at a fixed live set. No growth happens here, so
    // this isolates acquire()/release() from the grow() path insert also pays.
    template <template <class> class Map, class T>
    void churn_phase(const char* fin, const char* elem, const char* dens,
                     std::size_t live, std::size_t stride) {
        const std::size_t M = live * stride;
        using Key = typename Map<T>::key_type;

        Map<T> m;
        std::vector<Key> all;
        all.reserve(M);
        for (std::size_t i = 0; i < M; ++i)
            all.push_back(m.emplace(make_val<T>(i)));

        std::vector<Key> live_keys;
        live_keys.reserve(live);
        for (std::size_t i = 0; i < M; ++i) {
            if (i % stride == 0) live_keys.push_back(all[i]);
            else m.erase(all[i]);
        }

        std::mt19937_64 crng(RNG_SEED ^ 0x9E37u);
        print_row(label(fin, "churn").c_str(),
                  dens,
                  elem,
                  run(live,
                      [&] {
                          std::uint64_t cs = 0;
                          for (std::size_t c = 0; c < live; ++c) {
                              const std::size_t j = crng() % live;
                              m.erase(live_keys[j]);
                              live_keys[j] = m.emplace(make_val<T>(c));
                              cs += c;
                          }
                          return cs;
                      }));
    }

    // What to run, decoded from argv once and shared by every finder.
    struct Sel {
        bool ins, chu, itr, u32, p64, packed, half;
    };

    template <template <class> class Map>
    void run_variant(const char* fin, const Sel& s) {
        if (s.ins) {
            if (s.u32) insert_phase<Map, std::uint32_t>(fin, "u32", N);
            if (s.p64) insert_phase<Map, Payload64>(fin, "P64", N);
        }
        if (s.chu) {
            if (s.packed) {
                if (s.u32)
                    churn_phase<Map, std::uint32_t>(
                        fin,
                        "u32",
                        "packed",
                        N,
                        1);
                if (s.p64)
                    churn_phase<Map, Payload64>(
                        fin,
                        "P64",
                        "packed",
                        N,
                        1);
            }
            if (s.half) {
                if (s.u32)
                    churn_phase<Map, std::uint32_t>(
                        fin,
                        "u32",
                        "half",
                        N,
                        2);
                if (s.p64)
                    churn_phase<Map,
                        Payload64>(fin, "P64", "half", N, 2);
            }
        }
        if (s.itr) {
            if (s.packed) {
                if (s.u32)
                    iterate_phase<Map, std::uint32_t>(
                        fin,
                        "u32",
                        "packed",
                        N,
                        1);
                if (s.p64)
                    iterate_phase<Map, Payload64>(
                        fin,
                        "P64",
                        "packed",
                        N,
                        1);
            }
            if (s.half) {
                if (s.u32)
                    iterate_phase<Map, std::uint32_t>(
                        fin,
                        "u32",
                        "half",
                        N,
                        2);
                if (s.p64)
                    iterate_phase<Map, Payload64>(
                        fin,
                        "P64",
                        "half",
                        N,
                        2);
            }
        }
    }

    // (finder x storage) variants as one-arg Map metafunctions. split = the
    // default two-pool layout; soa = inline-meta (version co-located in the
    // value page).
    template <class T>
    using OffSplit =
    inco::SparseSlotMap<T, T, inco::HierarchicalBitmap, inco::SplitStore<T> >;
    template <class T>
    using OffSoA =
    inco::SparseSlotMap<T, T, inco::HierarchicalBitmap, inco::SoAStore<T> >;
    template <class T>
    using LiveAllocSplit =
    inco::SparseSlotMap<T, T, inco::LiveAllocBitmap, inco::SplitStore<T> >;
    template <class T>
    using LiveAllocSoA =
    inco::SparseSlotMap<T, T, inco::LiveAllocBitmap, inco::SoAStore<T> >;
}

extern "C++" int main(int argc, char** argv) {
    const char* phase = argc > 1 ? argv[1] : "all";
    const char* elem = argc > 2 ? argv[2] : "both";
    const char* dens = argc > 3 ? argv[3] : "both";
    // 4th arg filters the (finder x storage) variants so perf-stat counters
    // aren't mixed across instantiations. Matches a full name ("livealloc.soa"),
    // a finder ("off"/"livealloc"), or a storage ("split"/"soa"). Default all.
    const char* sel = argc > 4 ? argv[4] : "all";

    auto eq = [](const char* a, const char* b) {
        return std::strcmp(a, b) == 0;
    };
    Sel s{
        .ins = eq(phase, "all") || eq(phase, "insert"),
        .chu = eq(phase, "all") || eq(phase, "churn"),
        .itr = eq(phase, "all") || eq(phase, "iterate"),
        .u32 = eq(elem, "both") || eq(elem, "u32"),
        .p64 = eq(elem, "both") || eq(elem, "p64"),
        .packed = eq(dens, "both") || eq(dens, "packed"),
        .half = eq(dens, "both") || eq(dens, "half"),
    };

    // run this variant if `sel` is "all", or equals its full name / finder /
    // storage tag.
    auto want = [&](const char* full, const char* finder, const char* storage) {
        return eq(sel, "all") || eq(sel, full) || eq(sel, finder) || eq(
                   sel,
                   storage);
    };

    std::printf("perf_churn  N=%zu  (min of %d reps)\n", N, bench::REPS);
    bench::print_header("insert / churn / iterate (finder x storage)");

    if (want("off.split", "off", "split"))
        run_variant<
            OffSplit>("off.split", s);
    if (want("off.soa", "off", "soa")) run_variant<OffSoA>("off.soa", s);
    if (want("livealloc.split", "livealloc", "split"))
        run_variant<
            LiveAllocSplit>("livealloc.split", s);
    if (want("livealloc.soa", "livealloc", "soa"))
        run_variant<LiveAllocSoA>(
            "livealloc.soa",
            s);

    std::printf("[sink=%llu]\n",
                static_cast<unsigned long long>(bench::g_sink));
    return 0;
}