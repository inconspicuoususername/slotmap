#pragma once

#define PERF_ADAPTER(Struct, Name, Finder, StoreTmpl)                       \
    struct Struct {                                                          \
        static constexpr const char* name = Name;                           \
        static constexpr std::size_t max_slots = static_cast<std::size_t>(-1); \
        template <class T>                                                  \
        using Map = inco::SparseSlotMap<T, T, Finder, StoreTmpl<T>>;         \
        template <class T> using Key = typename Map<T>::key_type;            \
        template <class T> static Map<T> make() { return Map<T>{}; }         \
        template <class T> static Key<T> insert(Map<T>& m, const T& v) { return m.emplace_back(v); } \
        template <class T> static const T* find(Map<T>& m, Key<T> k) { return m.find(k); } \
        template <class T> static void erase(Map<T>& m, Key<T> k) { m.erase(k); } \
        template <class T, class F> static void for_each(Map<T>& m, F&& f) { \
            for (auto&& e : m) f(e.value);                                   \
        }                                                                   \
    }