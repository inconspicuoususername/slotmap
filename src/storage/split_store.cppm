module;

#include <cstddef>
#include <cstdint>
#include <utility>

export module slotmap.storage:split_store;
import :paged_store;

namespace inco {
    export template <
        class T,
        class V = std::uint32_t,
        std::size_t BytesPerPage = 16 * 1024,
        std::size_t MinSlots = 32
    >
    class SplitStore {
    public:
        using value_type = T;
        using version_type = V;

        static constexpr std::size_t page_slots =
            PagedStore<T, BytesPerPage, MinSlots>::page_slots;

        [[nodiscard]] T* at(std::size_t i) noexcept { return values_.at(i); }

        [[nodiscard]] const T* at(std::size_t i) const noexcept {
            return values_.at(i);
        }

        [[nodiscard]] V* version_at(std::size_t i) noexcept {
            return versions_.at(i);
        }

        [[nodiscard]] const V* version_at(std::size_t i) const noexcept {
            return versions_.at(i);
        }

        void ensure(std::size_t cap) {
            values_.ensure(cap);
            versions_.ensure(cap);
        }

        [[nodiscard]] std::size_t capacity() const noexcept {
            return values_.capacity();
        }

        template <class... Args>
        T* construct(std::size_t i, Args&&... args) {
            return values_.construct(i, std::forward<Args>(args)...);
        }

        void destroy(std::size_t i) noexcept { values_.destroy(i); }

    private:
        PagedStore<T, BytesPerPage, MinSlots> values_{};
        PagedStore<V, BytesPerPage, MinSlots> versions_{};
    };
}