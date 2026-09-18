module;
#include <bit>
#include <cstddef>
#include <utility>
export module slotmap:ranges;
import :concepts;
import :sparse;
import :utils;

namespace inco {
    export template <int K_num, class Fn>
    struct for_each_unrolled_closure {
        Fn fn;

        template <
            class T,
            class Tag, class Finder, class SlotStorage, class Iterator>
            requires IterativeLambda<Fn, T> && LiveBitmapView<Finder>
        void operator()(
            SparseSlotMap<T, Tag, Finder, SlotStorage, Iterator>& sparse) const {
            constexpr int K =
                K_num != -1 ? K_num : (sizeof(T) >= 32 ? 16 : 8);

            const auto& bitmap = sparse.free_;
            const auto& store = sparse.store_;

            const std::size_t total =
                utils::ceil_div(bitmap.capacity(), Finder::word_bits);

            for (std::size_t cap_i = 0; cap_i < total; ++cap_i) {
                typename Finder::word word = bitmap.word_at(cap_i);
                if (!word) continue;

                const std::size_t full_offset = cap_i * Finder::word_bits;
                auto* base = store.at(full_offset);

                while (std::popcount(word) >= K) {
                    int unroll[K];
#pragma GCC unroll 16
                    for (int i = 0; i < K; ++i) {
                        unroll[i] = std::countr_zero(word);
                        word &= word - 1;
                    }
#pragma GCC unroll 16
                    for (int i = 0; i < K; ++i)
                        fn(full_offset + static_cast<std::size_t>(unroll[i]),
                           base[unroll[i]]);
                }
                while (word) {
                    const auto bits = std::countr_zero(word);
                    fn(full_offset + static_cast<std::size_t>(bits), base[bits]);
                    word &= word - 1;
                }
            }
        }

        // map | unrolled(fn) == unrolled(fn)(map)
        template <
            class T,
            class Tag, class Finder, class SlotStorage, class Iterator>
        friend void operator|(
            SparseSlotMap<T, Tag, Finder, SlotStorage, Iterator>& sparse,
            const for_each_unrolled_closure& self) {
            self(sparse);
        }
    };

    template <int K_num = -1>
    struct for_each_unrolled_fn {
        // pipe version
        template <typename Fn>
        [[nodiscard]] constexpr auto operator()(Fn&& fn) const {
            return for_each_unrolled_closure<K_num, std::decay_t<Fn>>{
                std::forward<Fn>(fn)};
        }

        // call version
        template <typename Sparse, typename Fn>
        void operator()(Sparse& sparse, Fn&& fn) const {
            (*this)(std::forward<Fn>(fn))(sparse);
        }
    };

    // An optimized version of the regular iterator. Use this instead of the
    // iterator if you want absolute max iteration performance:
    //     sparse | inco::unrolled([](std::size_t i, auto& v) { ... });
    export inline constexpr for_each_unrolled_fn<> unrolled{};

    export template <int K>
    inline constexpr for_each_unrolled_fn<K> unrolled_k{};
}
