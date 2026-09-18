module;
#include <bit>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>
export module slotmap:storage.paged_store;
import :utils;
import :storage.page_pool;

namespace inco {
    // Paged pool with fixed size pages.
    // Store is pointer stable, since pages are not moved after allocation.
    // would have used boost deque but it default constructs on resize
    // also iirc neither boost deque nor segmented vector nor std deque use pow2 indexing
    export template <
        class T,
        std::size_t BytesPerPage = 64 * 1024,
        std::size_t MinSlots = 32,
        bool ZeroInit = true
    >
    class PagedStore {
    public:
        // number of T slots per page
        static constexpr std::size_t page_slots = utils::get_page_slots<T>(
            BytesPerPage,
            MinSlots
        );

        // allows the implementation to avoid doing i / page_slots
        static constexpr std::size_t page_shift = std::countr_zero(page_slots);
        static constexpr std::size_t page_mask = page_slots - 1;

        PagedStore() = default;

        PagedStore(const PagedStore&) = delete;

        PagedStore& operator=(const PagedStore&) = delete;

        PagedStore(PagedStore&& o) noexcept
            : pages_(std::move(o.pages_)),
              _hot_page(o._hot_page),
              _hot_base(o._hot_base) {
            o.reset_moved_from();
        }

        PagedStore& operator=(PagedStore&& o) noexcept {
            if (this != &o) {
                release_all();
                pages_ = std::move(o.pages_);
                _hot_page = o._hot_page;
                _hot_base = o._hot_base;
                o.reset_moved_from();
            }
            return *this;
        }

        ~PagedStore() { release_all(); }

        [[nodiscard]] T* at(const std::size_t i) noexcept {
            return slot_ptr(i >> page_shift, i & page_mask);
        }

        [[nodiscard]] const T* at(const std::size_t i) const noexcept {
            return const_cast<PagedStore*>(this)->at(i);
        }

        // alloc up to cap - 1 total capacity
        void ensure(const std::size_t cap) {
            const std::size_t want_pages = (cap + page_slots - 1) >> page_shift;
            while (pages_.size() < want_pages)
                pages_.push_back(new_page());
        }

        [[nodiscard]] std::size_t capacity() const noexcept {
            return pages_.size() * page_slots;
        }

        template <class... Args>
        T* construct(const std::size_t i, Args&&... args) {
            const std::size_t pg = i >> page_shift;
            if (pg != _hot_page) {
                _hot_base = reinterpret_cast<T*>(pages_[pg]->bytes);
                _hot_page = pg;
            }
            return std::construct_at(_hot_base + (i & page_mask),
                                     std::forward<Args>(args)...);
        }

        void destroy(const std::size_t i) noexcept { std::destroy_at(at(i)); }

    private:
        struct Page {
            alignas(T) std::byte bytes[page_slots * sizeof(T)];
        };

        using Pool = PagePool<sizeof(Page), alignof(Page)>;

        static Page* new_page() {
            Page* p = static_cast<Page*>(Pool::acquire());
            if constexpr (ZeroInit) std::memset(p->bytes, 0, sizeof(p->bytes));
            return p;
        }

        void release_all() noexcept {
            for (Page* p : pages_) Pool::release(p);
            pages_.clear();
        }

        void reset_moved_from() noexcept {
            pages_.clear();
            _hot_page = static_cast<std::size_t>(-1);
            _hot_base = nullptr;
        }

        T* slot_ptr(std::size_t page, std::size_t off) noexcept {
            return reinterpret_cast<T*>(pages_[page]->bytes) + off;
        }

        std::vector<Page*> pages_{};
        std::size_t _hot_page = static_cast<std::size_t>(-1);
        T* _hot_base = nullptr;
    };
}