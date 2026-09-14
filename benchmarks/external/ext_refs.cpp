// Cross-library reference adapters. Plain TU (no modules) so the header-only
// slotmaps -- one of which (twiggler) drags in Boost -- compile normally,
// away from the module purview. Each adapter is the thin shim run_lifecycle_elem
// expects; the shared driver in bench_common.hpp does the actual measuring.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "bench_common.hpp"

#include <spore/slot_map.hpp>       // sporacid    -- fixed-capacity hierarchical bitmap
#include <slot_map.h>               // SergeyMakeev -- page-based free-index reuse
#include <slotmap/slotmap.hpp>      // twiggler     -- skipfield fast iteration (Boost)
#include <slotmap/filtered.hpp>

using bench::Payload64;
using bench::make_val;
using bench::run_lifecycle_elem;

namespace {
    // sporacid is sized at compile time; CAP must exceed the highest high-water
    // we build (half density on N == 1<<19 gives M == 1<<20) with headroom --
    // filling to exactly capacity trips its full-map edge under NDEBUG.
    struct SporacidAd {
        static constexpr const char* name = "sporacid (bitmap)";
        static constexpr std::size_t CAP = 1u << 21;
        static constexpr std::size_t max_slots = CAP;

        template <class T> using Map = spore::slot_map_st<spore::slot_key, T, CAP>;
        template <class T> using Key = spore::slot_key;

        template <class T> static Map<T> make() { return Map<T>{}; }
        template <class T> static Key<T> insert(Map<T>& m, const T& v) { return m.emplace(v); }
        template <class T> static const T* find(Map<T>& m, Key<T> k) { return m.try_at(k); }
        template <class T> static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F> static void for_each(Map<T>& m, F&& f) {
            for (auto&& [k, v] : m) f(v);
        }
    };

    struct SergeyAd {
        static constexpr const char* name = "SergeyMakeev (paged)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T> using Map = dod::slot_map<T>;
        template <class T> using Key = typename dod::slot_map<T>::key;

        template <class T> static Map<T> make() { return Map<T>{}; }
        template <class T> static Key<T> insert(Map<T>& m, const T& v) { return m.emplace(v); }
        template <class T> static const T* find(Map<T>& m, Key<T> k) { return m.get(k); }
        template <class T> static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F> static void for_each(Map<T>& m, F&& f) {
            for (auto&& v : m) f(v);
        }
    };

    template <class U> using StdVec = std::vector<U>;

    // GROW = dynamic capacity, SKIPFIELD = twiggler's fast (holes-skipping)
    // iteration. IndexBits = IdBits - GenerationBits, and 32/16 (index bits 16)
    // is the widest that compiles on GCC 16: its evolve() does std::max(1, x)
    // where x is UInt, which only builds while UInt promotes to int, i.e. <= 16
    // bits. That in turn caps twiggler at 65535 slots, so its section runs at a
    // reduced live count. Anything larger is skipped via max_slots.
    struct TwigAd {
        static constexpr const char* name = "twiggler (skipfield)";
        static constexpr unsigned FLAGS =
            Twig::Container::SlotmapFlags::GROW |
            Twig::Container::SlotmapFlags::SKIPFIELD;
        static constexpr std::size_t max_slots = 65535;

        template <class T> using Map = Twig::Container::Slotmap<T, StdVec, 32, 16, FLAGS>;
        template <class T> using Key = typename Map<T>::Id;

        // start small so the insert phase actually exercises the grow path
        template <class T> static Map<T> make() { return Map<T>(4); }
        template <class T> static Key<T> insert(Map<T>& m, const T& v) { return m.push(v); }
        template <class T> static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }
        template <class T> static void erase(Map<T>& m, Key<T> k) { m.free(k); }

        template <class T, class F> static void for_each(Map<T>& m, F&& f) {
            auto live = Twig::Container::make_filtered(m);
            for (auto&& v : live) f(v);
        }
    };

    template <class Ad>
    void run_lib(std::size_t live) {
        run_lifecycle_elem<Ad, std::uint32_t>("u32", live);
        run_lifecycle_elem<Ad, Payload64>("P64", live);
    }
}

extern "C" void bench_external_sporacid() { run_lib<SporacidAd>(bench::N); }
extern "C" void bench_external_sergey() { run_lib<SergeyAd>(bench::N); }
// reduced live count: twiggler is capped at 65535 slots on GCC 16 (see TwigAd).
extern "C" void bench_external_twiggler() { run_lib<TwigAd>(1u << 15); }
