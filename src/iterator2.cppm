module;

#include <iterator>
#include <functional>
#include <span>

export module slotmap:iterator2323232;
import :bitmap;

namespace slotmap {
    //
    export template <bool DENSE, std::size_t BATCH_SIZE = 16>
    struct HierarchicalBitmapIterator {
        //required stl aliases

        using value_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = std::size_t;
        using pointer = void;
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::input_iterator_tag;

        using HB = HierarchicalBitmap;
        using Self = HierarchicalBitmapIterator;

        explicit
        HierarchicalBitmapIterator( //we start at the top
            HierarchicalBitmap* ptr) : level(ptr->_depth), _ptr(ptr) {
        }

        // access operators return current iterated value
        // reference operator*() const {
        //     return get_index_from_pos();
        // }
        //
        // pointer operator->() const {
        // }

        //prefix
        // HierarchicalBitmapIterator& operator++() {
        //     m_ptr++;
        //     return *this;
        // }
        //
        // //postfix
        // HierarchicalBitmapIterator operator++(int) {
        //     HierarchicalBitmapIterator tmp = *this;
        //     ++;
        //     return tmp;
        // }

        // friend bool operator==(const HierarchicalBitmapIterator& a,
        //                        const HierarchicalBitmapIterator& b) {
        //     return a._ptr == b._ptr;
        // }
        //
        // friend bool operator!=(const HierarchicalBitmapIterator& a,
        //                        const HierarchicalBitmapIterator& b) {
        //     return a._ptr != b._ptr;
        // }


        auto operator*() const -> std::span<const std::uint32_t> {
            return {_buf.data(), _count};
        }

        auto operator++() -> Self& {
            _count = next_batch(_buf);
            return *this;
        }

        bool operator==(std::default_sentinel_t) const { return _count == 0; }

    private:
        bool advance_to_next_word() {
            auto should_continue = false;
            if constexpr (DENSE) {
                ++_word_leaf_pos;
                if (_ptr->_capacity - 1 > _word_leaf_pos)
                    should_continue = true;
            } else {
                //TODO Sparse!!
            }
            _word = _ptr->_levels[HB::LEAF_LEVEL][_word_leaf_pos];
            return should_continue;
        }

        std::size_t next_batch(std::span<std::uint32_t, BATCH_SIZE> out) {
            std::size_t n = 0;
            while (n < BATCH_SIZE) {
                while (_word && n < BATCH_SIZE) {
                    const auto bit = std::countr_zero(_word);
                    out[n++] = _word_leaf_pos * 64 + bit;
                    _word &= _word - 1;
                }
                if (!advance_to_next_word())
                    break;
            }
            return n;
        }


        void test() {
        }


        HB::word _word;
        std::size_t _word_leaf_pos = 0;
        std::size_t level;

        HierarchicalBitmap* _ptr;

        std::array<std::uint32_t, BATCH_SIZE> _buf;
        std::size_t _count = 0;
    };
}