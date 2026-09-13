module;

#include <compare>
#include <cstdint>

export module slotmap:key;

namespace slotmap {
    // Default key layout
    export struct KeyLayout32_32 {
        using underlying = std::uint64_t;
        static constexpr int index_bits = 32;
        static constexpr int version_bits = 32;
    };

    // 32 bit  layout
    export struct KeyLayout20_12 {
        using underlying = std::uint32_t;
        static constexpr int index_bits = 20;
        static constexpr int version_bits = 12;
    };

    // tag only exists to make type instances different enum class style
    export template <class Tag, class Layout = KeyLayout32_32>
    struct Key {
        using underlying = typename Layout::underlying;
        static constexpr underlying index_mask =
            (underlying{1} << Layout::index_bits) - 1;

        // low index_bits = index
        // high version_bits = version
        // version 0 is null
        underlying raw = 0;

        [[nodiscard]] constexpr underlying index() const noexcept {
            return raw & index_mask;
        }

        [[nodiscard]] constexpr underlying version() const noexcept {
            return raw >> Layout::index_bits;
        }

        [[nodiscard]] constexpr bool valid() const noexcept {
            return version() != 0;
        }

        static constexpr Key
        make(underlying index, underlying version) noexcept {
            return Key{(version << Layout::index_bits) | (index & index_mask)};
        }
        
        [[nodiscard]] explicit constexpr operator underlying() const noexcept {
            return raw;
        }

        friend constexpr auto operator<=>(const Key&, const Key&) = default;
    };
}