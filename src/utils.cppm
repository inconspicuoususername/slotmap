module;
#include <bit>
#include <cstddef>
export module slotmap.utils;

namespace ic {
    // `std::pow` is not consteval so yeah thank you WG21
    export consteval double pow(const double base, const int exp) noexcept { // NOLINT(bugprone-easily-swappable-parameters)
        double res = 1.0;
        const bool negative = exp < 0;
        const int n = negative ? -exp : exp;

        for (int i = 0; i < n; ++i) {
            res *= base;
        }

        return negative ? (1.0 / res) : res;
    }

    // 128 / 64 = 2, 129 / 64 = 3
    export constexpr std::size_t ceil_div(
        const std::size_t a,
        const std::size_t b
    ) noexcept {
        return (a + b - 1) / b;
    }

    export void prefetch_read(const void* p) noexcept {
#if defined(__GNUC__) || defined(__clang__)
        //pointer
        __builtin_prefetch(
            p,
            //prefill pointer
            0,
            // 0 = prepare for read
            3 // 3 = prefill l1, l2, l3, 0 = prefill l1 and evict asap
        );
#else
        (void)p; //TODO: use msvc mm_prefetch
#endif
    }
}