module;
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
export module slotmap.storage:soa_store;
import slotmap.utils;
import :page_pool;

namespace inco {
    // TODO subclass paged instead of CTRL + C
    export template <
        class T,
        class V = std::uint32_t,
        std::size_t BytesPerPage = 64 * 1024,
        std::size_t MinSlots = 32
    >
    class SoAStore {
    public:
        using value_type = T;
        using version_type = V;
        static constexpr std::size_t page_slots = get_page_slots<T>(
            BytesPerPage,
            MinSlots
        );

        static constexpr std::size_t page_shift = std::countr_zero(page_slots);
        static constexpr std::size_t page_mask = page_slots - 1;

        static constexpr std::size_t values_bytes = page_slots * sizeof(T);
        static constexpr std::size_t versions_off =
            (values_bytes + alignof(V) - 1) & ~(alignof(V) - 1);
        static constexpr std::size_t page_bytes =
            versions_off + page_slots * sizeof(V);

        SoAStore() = default;

        SoAStore(const SoAStore&) = delete;

        SoAStore& operator=(const SoAStore&) = delete;

        SoAStore(SoAStore&& o) noexcept
            : pages_(std::move(o.pages_)),
              _hot_page(o._hot_page),
              _hot_base(o._hot_base) {
            o.reset_moved_from();
        }

        SoAStore& operator=(SoAStore&& o) noexcept {
            if (this != &o) {
                release_all();
                pages_ = std::move(o.pages_);
                _hot_page = o._hot_page;
                _hot_base = o._hot_base;
                o.reset_moved_from();
            }
            return *this;
        }

        ~SoAStore() { release_all(); }

        [[nodiscard]] T* at(const std::size_t i) noexcept {
            return value_base(i >> page_shift) + (i & page_mask);
        }

        [[nodiscard]] const T* at(const std::size_t i) const noexcept {
            return const_cast<SoAStore*>(this)->at(i);
        }

        [[nodiscard]] V* version_at(const std::size_t i) noexcept {
            return version_base(i >> page_shift) + (i & page_mask);
        }

        [[nodiscard]] const V* version_at(const std::size_t i) const noexcept {
            return const_cast<SoAStore*>(this)->version_at(i);
        }

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
                _hot_base = value_base(pg);
                _hot_page = pg;
            }
            return std::construct_at(_hot_base + (i & page_mask),
                                     std::forward<Args>(args)...);
        }

        void destroy(const std::size_t i) noexcept { std::destroy_at(at(i)); }

    private:
        // over-align to a cache line: with the odd (values+versions) page size,
        // plain alignof(T) can land values off a 64B boundary and make every
        // Payload64 straddle two lines, which wrecks iteration.
        static constexpr std::size_t page_align =
            alignof(T) > 64 ? alignof(T) : 64;

        struct alignas(page_align) Page {
            alignas(T) std::byte bytes[page_bytes];
        };

        using Pool = PagePool<sizeof(Page), alignof(Page)>;
        
        static Page* new_page() {
            Page* p = static_cast<Page*>(Pool::acquire());
            std::memset(p->bytes + versions_off, 0, page_bytes - versions_off);
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

        T* value_base(std::size_t page) noexcept {
            return reinterpret_cast<T*>(pages_[page]->bytes);
        }

        V* version_base(std::size_t page) noexcept {
            return reinterpret_cast<V*>(pages_[page]->bytes + versions_off);
        }

        std::vector<Page*> pages_{};
        std::size_t _hot_page = static_cast<std::size_t>(-1);
        T* _hot_base = nullptr;
    };
}