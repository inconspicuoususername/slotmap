module;

#include <concepts>
#include <cstddef>
#include <cstdint>

export module slotmap:concepts;

namespace inco {
    export template <class F>
    concept FreeFinder = requires(F f, const F cf, std::size_t n)
    {
        { f.acquire() } -> std::same_as<std::size_t>;
        { F::npos } -> std::convertible_to<std::size_t>;
        { f.release(n) };
        { f.grow(n) };
        { cf.capacity() } -> std::convertible_to<std::size_t>;
    };

    export template <class F>
    concept LiveBitmapView = requires(const F cf, std::size_t n)
    {
        typename F::word;
        { cf.word_at(n) } -> std::convertible_to<std::uint64_t>;
        { cf.capacity() } -> std::convertible_to<std::size_t>;
        { F::word_bits } -> std::convertible_to<int>;
    };

    // currently .at returns a ptr but prolly should return const ref or something ismilar
    export template <class S, class T>
    concept Storage = requires(S s, const S cs, std::size_t n)
    {
        { s.at(n) } -> std::same_as<T*>;
        { s.ensure(n) };
        { cs.capacity() } -> std::convertible_to<std::size_t>;
    };

    export template <class Fn, class T>
    concept IterativeLambda = std::regular_invocable<Fn, std::size_t, T&>;
}