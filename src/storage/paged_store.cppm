module;

#include <bit>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

export module slotmap.storage:paged_store;

namespace inco {
    // Paged pool with fixed size pages
    //store is pointer stable, since pages are not moved after allocation
    // would have used boost deque but it default constructs on resize
    // also iirc neither boost deque nor segmented vector or std deque use pow 2 indexing
    export template<
        class T,
        std::size_t BytesPerPage = 16 * 1024,
        std::size_t MinSlots = 32
    >
    class PagedStore {
    public:
        // number of T slots per page
        static constexpr std::size_t page_slots = [] {
            const auto element_size = sizeof(T) ? sizeof(T) : 1;

            // either clamp to min slots or use the total bytes
            std::size_t number_slots = std::max(
                BytesPerPage / element_size,
                MinSlots
            );

            // use a pow 2 page sloot number
            return std::bit_floor(number_slots);
        }();

        // allows the implementation to avoid doing i / page_slots
        static constexpr std::size_t page_shift = std::countr_zero(page_slots);
        static constexpr std::size_t page_mask = page_slots - 1;

        [[nodiscard]] T *at(std::size_t i) noexcept {
            return slot_ptr(i >> page_shift, i & page_mask);
        }

        [[nodiscard]] const T *at(std::size_t i) const noexcept {
            return const_cast<PagedStore *>(this)->at(i);
        }

        // alloc up to cap - 1 total capacity
        void ensure(std::size_t cap) {
            const std::size_t want_pages = (cap + page_slots - 1) >> page_shift;
            while (pages_.size() < want_pages)
                pages_.push_back(make_zeroed_page());
        }

        [[nodiscard]] std::size_t capacity() const noexcept {
            return pages_.size() * page_slots;
        }

        template<class... Args>
        T *construct(std::size_t i, Args &&... args) {
            return std::construct_at(at(i), std::forward<Args>(args)...);
        }

        void destroy(std::size_t i) noexcept { std::destroy_at(at(i)); }

    private:
        struct Page {
            alignas(T) std::byte bytes[page_slots * sizeof(T)];
        };

        static std::unique_ptr<Page> make_zeroed_page() {
            auto p = std::make_unique<Page>();
            std::memset(p->bytes, 0, sizeof(p->bytes));
            return p;
        }

        T *slot_ptr(std::size_t page, std::size_t off) noexcept {
            return reinterpret_cast<T *>(pages_[page]->bytes) + off;
        }

        std::vector<std::unique_ptr<Page> > pages_{};
    };
}