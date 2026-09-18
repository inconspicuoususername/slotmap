module;

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <expected>
#include <functional>
#include <iostream>
#include <utility>

export module slotmap:sparse;

import :key;
import :error;
import :free;
import :storage;
import :concepts;
import :iterators;
import :utils;

namespace inco {
    export template <
        typename T,
        typename Tag = T,
        class Finder = LiveAllocBitmap,
        class SlotStorage = SplitStore<T>,
        class Iterator = void>
        requires FreeFinder<Finder> &&
                 Storage<SlotStorage, T>

    class SparseSlotMap {
    public:
        using key_type = Key<Tag>;
        using version_t = std::uint32_t;

        using finder_t = Finder;
        using storage_t = SlotStorage;

        using IteratorType = std::conditional_t<
            std::is_same_v<Iterator, void>,
            PageWalkIter<T, SlotStorage, Finder>,
            Iterator
        >;

        static_assert(
            std::constructible_from<IteratorType, finder_t&, storage_t&>,
            "The provided Iterator type must be constructible from the required arguments.");

        template <class... Args>
        requires std::constructible_from<T, Args...>
        key_type emplace_back(Args&&... args) {
            std::size_t slot = free_.acquire();
            if (slot == Finder::npos) [[unlikely]] {
                slot = grow_and_reacquire();
                //we're screwed
                if (slot == Finder::npos) return key_type{}; // invalid
            }
            const std::size_t idx = slot;

            // get version of idx
            version_t v = *store_.version_at(idx);
            // if first time, create it for the first time
            if (v == 0) {
                v = 1;
                *store_.version_at(idx) = 1;
            }
            // then construct with forward
            store_.construct(idx, std::forward<Args>(args)...);
            ++size_;
            return key_type::make(
                static_cast<typename key_type::underlying>(idx),
                v);
        }

        template <class... Args>
        ResultType<key_type> try_emplace_back(Args&&... args) {
            key_type k = emplace_back(std::forward<Args>(args)...);
            if (!k.valid()) return unexpected("index space exhausted");
            return k;
        }

        // Fast find lookup
        [[nodiscard]] T* find(key_type k) noexcept {
            const auto idx = static_cast<std::size_t>(k.index());
            if (!k.valid() || idx >= store_.capacity()) return nullptr;

            if (const version_t stored = *store_.version_at(idx);
                stored != static_cast<version_t>(k.version())) {
#ifdef IC_SLOTMAP_DEBUG
                assert(
                    !(k.valid() && stored > static_cast<version_t>(k.version())
                        + 1u
                        && "key held across too many version bumps in slotmap"
                    ));
#endif
                return nullptr;
            }
            return store_.at(idx);
        }

        // const ver
        [[nodiscard]] const T* find(key_type k) const noexcept {
            return const_cast<SparseSlotMap*>(this)->find(k);
        }

        // slow version
        [[nodiscard]] std::optional<T> at(key_type k) const {
            if (T* p = find(k)) return std::reference_wrapper<T>{*p};
            return std::nullopt;
        }

        bool erase(key_type k) {
            T* p = find(k);
            if (!p) return false;

            const std::size_t idx = static_cast<std::size_t>(k.index());

            store_.destroy(idx);
            // invalidate every outstanding key to this slot
            ++*store_.version_at(idx);
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
            store_.ensure(size);
            free_.grow(size);
        }

        // expand by N elements
        void expand(const std::size_t size) noexcept {
            const std::size_t cap = free_.capacity();
            reserve(cap + size);
        }


        IteratorType begin() {
            return IteratorType{free_, store_};
        }

        [[nodiscard]]
        std::default_sentinel_t end() const noexcept {
            return std::default_sentinel;
        }

        // template <, class Fn>
        // requires IterativeLambda<Fn, T>
        // void for_each_fast(Fn&& fn) {
        //     inco::unrolled(free_, store_, std::forward<Fn>(fn));
        //     // inco::for_each_unrolled<T, Finder, SlotStorage, Fn, K>(
        //     //     free_,
        //     //     store_,
        //     //     std::forward<Fn>(fn));
        // }

        template <class Lefunc, class Lambda>
        void fn_iterator(Lefunc&& walk, Lambda&& lambda) {
            std::forward<Lefunc>(walk)(
                free_,
                store_,
                std::forward<Lambda>(lambda));
        }

        // TODO per-page rwlock on bitmap/version writes wit page dir

    private:
        template <int, class> friend struct for_each_unrolled_closure;
        [[gnu::cold, gnu::noinline]]
        std::size_t grow_and_reacquire() {
            expand(get_growth_factor());
            return free_.acquire();
        }

        [[nodiscard]] inline std::size_t get_growth_factor() const noexcept {
            // constexpr std::size_t max_growth_factor = 16;
            // const auto factor = utils::ceil_div(size() + 1, SlotStorage::page_slots);
            // // std::cout << " Factor " << factor << "\n";
            // const auto growth_factor =  std::min(factor * factor, max_growth_factor);
            constexpr auto growth_factor = 1;
            return SlotStorage::page_slots * growth_factor;
        }

        Finder free_{};
        SlotStorage store_{};
        std::size_t size_ = 0;
    };
}