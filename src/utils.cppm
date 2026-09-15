module;
#include <algorithm>
#include <bit>
#include <cstddef>
export module slotmap.utils;

namespace inco {
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

    export template <class T>
    consteval std::size_t get_page_slots(
        const std::size_t bytes_per_page,
        const std::size_t min_slots
    ) {
        const auto element_size = sizeof(T) ? sizeof(T) : 1;

        // either clamp to min slots or use the total bytes
        std::size_t number_slots = std::max(
            bytes_per_page / element_size,
            min_slots
        );

        // use a pow 2 page sloot number
        return std::bit_floor(number_slots);
    }
}