module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

export module slotmap:sparse;

import :key;
import result;
import slotmap.free;
import slotmap.storage;
import slotmap.concepts;
import slotmap.iterators;

namespace inco {
    export template <
        class T,
        class Tag = T,
        class Finder = HierarchicalBitmap,
        template <class> class Store = PagedStore,
        class Iterator = void>
        requires FreeFinder<Finder> &&
                 Storage<Store<T>, T>

    class SparseSlotMap {
    public:
        using key_type = Key<Tag>;
        using version_t = std::uint32_t;

        template <class U>
        using Ref = std::reference_wrapper<U>;

        using IteratorType = std::conditional_t<
            std::is_same_v<Iterator, void>,
            PageWalkIter<T, Store<T> >,
            Iterator
        >;

        static_assert(std::constructible_from<IteratorType, Finder&, Store<T>&>,
                      "The provided Iterator type must be constructible from the required arguments.")
        ;

        template <class... Args>
        result::Result<key_type> try_emplace(Args&&... args) {
            auto slot = free_.acquire();
            if (!slot) {
                // when out of capacity, grow one page and retry
                const std::size_t next = Store<T>::page_slots;
                expand(next);
                slot = free_.acquire();

                //we're screwed
                if (!slot)
                    return result::fail("index space exhausted");
            }
            const std::size_t idx = *slot;

            // get version of idx
            version_t v = *versions_.at(idx);
            // if first time, create it for the first time
            if (v == 0) {
                v = 1;
                *versions_.at(idx) = 1;
            }
            // then construct with forward
            values_.construct(idx, std::forward<Args>(args)...);
            ++size_;
            return key_type::make(
                static_cast<typename key_type::underlying>(idx),
                v);
        }

        // Fast find lookup
        [[nodiscard]] T* find(key_type k) noexcept {
            const auto idx = static_cast<std::size_t>(k.index());
            if (!k.valid() || idx >= versions_.capacity()) return nullptr;

            if (const version_t stored = *versions_.at(idx);
                stored != static_cast<version_t>(k.version())) {
#ifdef IC_SLOTMAP_DEBUG
                assert(
                    !(k.valid() && stored > static_cast<version_t>(k.version())
                        + 1u
                        && "stale handle held across too many version bumps in slotmap"
                    ));
#endif
                return nullptr;
            }
            return values_.at(idx);
        }

        // const ver
        [[nodiscard]] const T* find(key_type k) const noexcept {
            return const_cast<SparseSlotMap*>(this)->find(k);
        }

        // Result<..> version of find
        [[nodiscard]] result::Result<Ref<T> > at(key_type k) const {
            if (T* p = find(k)) return Ref<T>{*p};
            return result::fail("invalid key provided");
        }

        bool erase(key_type k) {
            T* p = find(k);
            if (!p) return false;
            const std::size_t idx = static_cast<std::size_t>(k.index());
            values_.destroy(idx);
            // invalidate every outstanding key to this slot
            ++*versions_.at(idx);
            free_.release(idx);
            --size_;
            return true;
        }

        [[nodiscard]] bool contains(key_type k) const noexcept {
            return find(k) != nullptr;
        }

        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

        // reserve at least N elements
        void reserve(std::size_t size) noexcept {
            values_.ensure(size);
            versions_.ensure(size);
            free_.grow(size);
        }

        // expand by N elements
        void expand(const std::size_t size) noexcept {
            const std::size_t cap = free_.capacity();
            reserve(cap + size);
        }


        IteratorType begin() { return IteratorType{free_, values_}; }

        [[nodiscard]] std::default_sentinel_t end() const noexcept {
            return std::default_sentinel;
        }

        // TODO per-page rwlock on bitmap/version writes wit page dir

    private:
        Finder free_{};
        Store<T> values_{};
        Store<version_t> versions_{};
        std::size_t size_ = 0;
    };
}