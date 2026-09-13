module;

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <memory>

export module slotmap:iterator;

import :bitmap;
import :storage;
import :utils;
import :policy;

namespace slotmap {
    // rough logic description
    //
    // dense-iterate
    // for each bottom level, bit skip and yield
    //
    // sparse-iterate
    // acquire-like logic, begin at the top, and descend
    // once we hit the leaf,
    // while true bit skip (w & w-1) and yield
    // if we're done, ascend.
    // repeat until we hit a 1, then descend again until we hit the leaf
    // repeat until we're done
    //
    class HierarchichalBitmapIterator {
    public:
        using HB = HierarchicalBitmap;
        using word = HB::word;

        HierarchichalBitmapIterator() = default;

        explicit HierarchichalBitmapIterator(const HB& bm) noexcept
            : _bitmap(&bm),
              _num_leaf_words(ic::ceil_div(bm.capacity(), HB::word_bits)) {
            if (_num_leaf_words) _current_word = bm.word_at(0);
        }

        // fills out with up to n next indices
        // returns total indices or 0 if we're done
        // TODO implement sparse logic sumwhere here
        template <std::size_t N>
        std::size_t
        fill_next_batch(std::array<std::uint32_t, N>& out) noexcept {
            std::size_t n = 0;
            while (true) {
                while (_current_word) {
                    const int bit = std::countr_zero(_current_word);
                    out[n++] = static_cast<std::uint32_t>(
                        _word_idx * HB::word_bits + bit);
                    _current_word &= _current_word - 1;
                    if (n == N) return n;
                }
                // no more words left
                if (++_word_idx >= _num_leaf_words) return n;
                _current_word = _bitmap->word_at(_word_idx);
            }
        }

    private:
        const HB* _bitmap = nullptr;
        std::size_t _num_leaf_words = 0;
        std::size_t _word_idx = 0;
        word _current_word = 0;
    };

    // iterate one batch while prefilling another
    export template <
        std::size_t Batch = 16,
        class T,
        std::size_t BytesPerPage,
        std::size_t MinSlots,
        class Fn
    >
    void for_each_prefetched(const HierarchicalBitmap& bitmap,
                             PagedStore<T, BytesPerPage, MinSlots>& store,
                             Fn&& fn) {
        HierarchichalBitmapIterator iter{bitmap};
        std::array<std::uint32_t, Batch> batch_a{}, batch_b{};
        auto* current_batch_ptr = &batch_a;
        auto* next_batch_ptr = &batch_b;
        auto& current_batch = *current_batch_ptr;
        auto& next_batch = *next_batch_ptr;

        std::size_t curr_batch_sz = iter.fill_next_batch(current_batch);
        for (std::size_t i = 0; i < curr_batch_sz; ++i)
            // pre fill first batch
            ic::prefetch_read(store.at(current_batch[i]));

        while (curr_batch_sz) {
            const std::size_t next_batch_sz = iter.fill_next_batch(next_batch);

            // prefill next batch
            for (std::size_t i = 0; i < next_batch_sz; ++i)
                ic::prefetch_read(store.at(next_batch[i]));

            //eat
            for (std::size_t i = 0; i < curr_batch_sz; ++i) {
                const std::size_t idx = current_batch[i];
                fn(idx, *store.at(idx));
            }
            std::swap(current_batch_ptr, next_batch_ptr);
            curr_batch_sz = next_batch_sz;
        }
    }

    export template <class T>
    struct SlotMapIteratorEntry {
        std::size_t index;
        T& value;
    };

    export template <
        class T,
        std::size_t BytesPerPage,
        std::size_t MinSlots,
        std::size_t Batch = 16>
    class StorageIterator {
    public:
        using entry = SlotMapIteratorEntry<T>;
        using value_type = entry;
        using reference = entry;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        //input iterator bcz its single pass
        using iterator_concept = std::input_iterator_tag;


        StorageIterator() = default;

        StorageIterator(
            const HierarchicalBitmap& bm,
            PagedStore<T, BytesPerPage, MinSlots>& store
        ) noexcept : _bitmap_iterator(bm), _store(&store) {
            refill();
        }

        reference operator*() const noexcept {
            const std::size_t idx = _buf[_cursor];
            return entry{
                .index = idx, .value = *_store->at(idx)
            };
        }

        StorageIterator& operator++() noexcept {
            if (++_cursor == _count) refill();
            return *this;
        }

        void operator++(int) noexcept { ++*this; }

        bool operator==(std::default_sentinel_t) const noexcept {
            return _count == 0;
        }

    private:
        void refill() noexcept {
            _cursor = 0;
            _count = _bitmap_iterator.fill_next_batch(_buf);
            for (std::size_t i = 0; i < _count; ++i)
                ic::prefetch_read(_store->at(_buf[i]));
        }

        HierarchichalBitmapIterator _bitmap_iterator{};
        PagedStore<T, BytesPerPage, MinSlots>* _store = nullptr;
        std::array<std::uint32_t, Batch> _buf{};
        std::size_t _count = 0;
        std::size_t _cursor = 0;
    };

    export template <
        class T,
        class Store,
        std::size_t Batch = 32
    > requires Storage<Store, T>
    class StorageIteratorFast {
    public:
        using entry = SlotMapIteratorEntry<T>;
        using value_type = entry;
        using reference = entry;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        //input iterator bcz its single pass
        using iterator_concept = std::input_iterator_tag;

        using batch_array = std::array<std::uint32_t, Batch>;
        using batch_array_ptr = batch_array*;

        StorageIteratorFast(
            Store& store,
            const HierarchicalBitmap& bm
        ) noexcept
            : _bitmap_iterator(bm),
              _store(&store),
              _current_ptr(&current_),
              _next_ptr(&next_) {
            fill(_current_ptr, _current_count);
            fill(_next_ptr, _next_count);
        }

        entry operator*() const {
            std::size_t idx = _current_ptr->at(_cursor);
            return entry{
                .index = idx, .value = *_store->at(idx)
            };
        }

        StorageIteratorFast& operator++() noexcept {
            if (++_cursor != _current_count)
                return *this;

            std::swap(_current_ptr, _next_ptr);
            _current_count = _next_count;
            _cursor = 0;

            fill(_next_ptr, _next_count);

            return *this;
        }


        bool operator==(std::default_sentinel_t) const noexcept {
            return _current_count == 0;
        }

    private:
        void fill(batch_array_ptr& buf,
                  std::size_t& count) noexcept {
            count = _bitmap_iterator.fill_next_batch(*buf);

            for (std::size_t i = 0; i < count; ++i)
                ic::prefetch_read(_store->at(buf->at(i)));
        }

        HierarchichalBitmapIterator _bitmap_iterator{};
        Store* _store = nullptr;

        batch_array current_{};
        batch_array next_{};

        batch_array_ptr _current_ptr = nullptr;
        batch_array_ptr _next_ptr = nullptr;

        std::size_t _current_count = 0;
        std::size_t _next_count = 0;
        std::size_t _cursor = 0;
    };
}