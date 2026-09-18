module;
#include <cstddef>
#include <utility>


#include <spore/slot_map.hpp>
#include <slot_map.h>
#include <slotmap/slotmap.hpp>
#include <slotmap/filtered.hpp>

export module adapters;
import slotmap;

export namespace adapters {
    struct IncoAd {
        static constexpr const char* name =
            "inco (HierarchichalBitmap)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = inco::SparseSlotMap<T>;
        template <class T>
        using Key = typename Map<T>::key_type;

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

    struct IncoLiveAllocAd {
        static constexpr const char* name =
            "inco (LiveAllocBitmap)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = inco::SparseSlotMap<T, T, inco::LiveAllocBitmap>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) {
            return m.emplace(v);
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

    template <
        class T,
        template <class, class, class> class Iterator,
        class Finder = inco::LiveAllocBitmap,
        class Store = inco::SplitStore<T> >
    using SlotMapMitDasIterator = inco::SparseSlotMap<T, T, Finder, Store,
        Iterator<T, Store, Finder> >;


    struct IncoUnrollIteratorAd {
        static constexpr const char* name =
            "inco (LiveAllocBitmap)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);
        template <class T>
        using Map = SlotMapMitDasIterator<T, inco::UnrolledPageWalkIter>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) {
            return m.emplace(v);
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
    
    struct IncoPrefetchIterAd {
        static constexpr const char* name = "inco (PrefetchIter)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = SlotMapMitDasIterator<T, inco::PrefetchPageWalkIter>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) { return m.emplace(v); }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            for (auto&& e : m) f(e.value);
        }
    };

    struct IncoFnAd {
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);
        template <class T>
        using Map = inco::SparseSlotMap<T, T, inco::LiveAllocBitmap>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) { return m.emplace(v); }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }
    };

#define LAMBDA_FN_ITERATOR(fn_name)                                 \
    static constexpr const char* name = #fn_name;                   \
            template <class T, class F>                             \
    static void for_each(Map<T>& m, F&& f) {                        \
            m.fn_iterator([](auto& bm, auto& store, auto&& fn) {    \
                                fn_name(                            \
                                  bm,                               \
                                  store,                            \
                                  std::forward<decltype(fn)>(fn));  \
                          },                                        \
                          [&](std::size_t, const T& v) { std::forward<F>(f)(v); });  \
        }

    struct IncoWalkFnAd : IncoFnAd {
        LAMBDA_FN_ITERATOR(inco::for_each_walk);
    };

    struct IncoPrefetchFnAd : IncoFnAd {
        LAMBDA_FN_ITERATOR(inco::for_each_prefetched);
    };

    struct IncoExpandFnAd : IncoFnAd {
        LAMBDA_FN_ITERATOR(inco::for_each_expanded);
    };

    struct IncoUnrollFnAd {
        static constexpr const char* name = "inco (UnrollFn)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = inco::SparseSlotMap<T, T, inco::LiveAllocBitmap>;
        template <class T>
        using Key = typename Map<T>::key_type;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) { return m.emplace(v); }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            m.for_each_fast([&](std::size_t, const T& v) { f(v); });
        }
    };

    struct IncoAVXFnAd : IncoFnAd {
        LAMBDA_FN_ITERATOR(inco::for_each_avx);
    };

    struct IncoAVXLanesFnAd : IncoFnAd {
        LAMBDA_FN_ITERATOR(inco::for_each_avx_lanes);
    };

    struct IncoSoAAd {
        static constexpr const char* name =
            "inconspicuoususername (LiveAlloc+SoA)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = inco::SparseSlotMap<T, T, inco::LiveAllocBitmap,
            inco::SoAStore<T> >;
        template <class T>
        using Key = typename Map<T>::key_type;

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

    // sporacid is sized at compile time; CAP must exceed the highest high-water
    // we build (half density on N == 1<<19 gives M == 1<<20) with headroom --
    // filling to exactly capacity trips its full-map edge under NDEBUG.
    struct SporacidAd {
        static constexpr const char* name = "sporacid (bitmap)";
        static constexpr std::size_t CAP = 1u << 21;
        static constexpr std::size_t max_slots = CAP;

        template <class T>
        using Map = spore::slot_map_st<spore::slot_key, T, CAP>;
        template <class T>
        using Key = spore::slot_key;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) { return m.emplace(v); }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.try_at(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            for (auto&& [k, v] : m) f(v);
        }
    };

    struct SergeyAd {
        static constexpr const char* name = "SergeyMakeev (paged)";
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1);

        template <class T>
        using Map = dod::slot_map<T>;
        template <class T>
        using Key = typename dod::slot_map<T>::key;

        template <class T>
        static Map<T> make() { return Map<T>{}; }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) { return m.emplace(v); }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.get(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.erase(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            for (auto&& v : m) f(v);
        }
    };

    template <class U>
    using StdVec = std::vector<U>;

    // Limited at 65535 slots
    struct TwigAd {
        static constexpr const char* name = "twiggler (skipfield)";
        static constexpr unsigned FLAGS =
            Twig::Container::SlotmapFlags::GROW |
            Twig::Container::SlotmapFlags::SKIPFIELD;
        static constexpr std::size_t max_slots = 65535;

        template <class T>
        using Map = Twig::Container::Slotmap<T, StdVec, 32, 16, FLAGS>;
        template <class T>
        using Key = typename Map<T>::Id;

        // start small so the insert phase actually exercises the grow path
        template <class T>
        static Map<T> make() { return Map<T>(4); }

        template <class T>
        static Key<T> insert(Map<T>& m, const T& v) { return m.push(v); }

        template <class T>
        static const T* find(Map<T>& m, Key<T> k) { return m.find(k); }

        template <class T>
        static void erase(Map<T>& m, Key<T> k) { m.free(k); }

        template <class T, class F>
        static void for_each(Map<T>& m, F&& f) {
            auto live = Twig::Container::make_filtered(m);
            for (auto&& v : live) f(v);
        }
    };
}