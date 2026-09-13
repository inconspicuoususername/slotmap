module;

#include <concepts>
#include <cstddef>
#include <optional>

export module slotmap:policy;

namespace slotmap {
    // Bitmap and FreeList concept
    export template <class F>
    concept FreeFinder = requires(F f, const F cf, std::size_t n)
    {
        { f.acquire() } -> std::same_as<std::optional<std::size_t> >;
        { f.release(n) };
        { f.grow(n) };
        { cf.capacity() } -> std::convertible_to<std::size_t>;
    };

    // Storage concept
    // currently .at returns a ptr but prolly should return const ref or something ismilar
    export template <class S, class T>
    concept Storage = requires(S s, const S cs, std::size_t n)
    {
        { s.at(n) } -> std::same_as<T*>;
        { s.ensure(n) };
        { cs.capacity() } -> std::convertible_to<std::size_t>;
    };
}