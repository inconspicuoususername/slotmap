module;

#include <array>
#include <bit>
#include <cstdint>
#include <vector>
#include <cassert>
#include <cmath>

#include "../macros.h"

export module slotmap.free:bitmap;
import slotmap.utils;

constexpr bool TESTFLAG = true;

namespace inco {
    // Hierarchical summary bitmap
    //
    // Contains an array of "levels" of 64-bit words, meaning an array of vectors, where
    // index 0 is the leaf array, and each subequent index is less and less specific.
    // Each parent word corresponds to 64 child words/slots, and each bit in the parent being set to 1
    // means that the child word is full. So each parent contains a "summary" about its children
    //
    // a great property of this structure is that acquire() intrinsically always returns the lowest free slot
    // this always keeps the live set packed for cache locality and keeps the high-water mark (and thus this
    // bitmap) minimal.
    export class HierarchicalBitmap {
    public:
        using word = std::uint64_t;

        using slot_index = std::size_t;
        using word_index = std::size_t;
        //
        // enum class bit_index : std::size_t {};
        // enum class word_index : std::size_t {};

        static constexpr int word_bits = 64;
        static constexpr word full = ~word{0};
        static constexpr std::size_t max_levels = 6; // 64^6 > 2^32, cca 68 bil
        static constexpr std::size_t max_items =
            pow(word_bits, max_levels);

        static constexpr std::size_t LEAF_LEVEL = 0;


        // Previously was an std optional, but std optional was returned in an xmm register
        // caller then extracts the optional to the stack, and reloads to rax and rdx
        // thank you sys v zero cost abstractions my ass
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        // return lowest free slot npos when out of capacity
        // (the caller should grow() and retry).

        [[nodiscard]] FORCE_INLINE std::size_t acquire() noexcept {
            const std::size_t slot = descend();
            if (slot == npos) return npos;
            return occupy(slot);
        }

        FORCE_INLINE
        void release(const slot_index slot) noexcept {
            const word_index word_index = slot / word_bits;
            const slot_index bit_offset = slot % word_bits;

            const bool was_full = (get_word(LEAF_LEVEL, word_index) == full);

            //clear bit
            clear_word_bit(LEAF_LEVEL, word_index, bit_offset);

            // if the word was previously full, the parent bit was set, so propagate
            if (was_full) propagate_clear_up(word_index);
        }

        // grow so at least `slots` indices are representable
        void grow(const std::size_t slots) {
            if (slots <= _capacity) return;

            //convert requested slots into words. ceil div because any overflow must be a new word
            // rather than the default floor of integer division
            const std::size_t leaf_words = ceil_div(slots, word_bits);

            if (_depth == 0) _depth = 1;
            // new leaf words should be initialized to 0
            _levels[LEAF_LEVEL].resize(leaf_words, 0);

            //now go through each level and update
            word_index child_words = leaf_words;
            for (std::size_t level = 1; child_words > 1; ++level) {
                // e.g. 3 words for 192 requested slots
                const std::size_t need = ceil_div(child_words, word_bits);
                if (level < _depth) {
                    // if we're still in the already claimed levels, just expand it so that
                    // it fits the new per-level need
                    _levels[level].resize(need, 0);
                } else {
                    // if we're past the current depth, it means that we cannot satsify the summarization
                    // with our current levels. For example, if we only had one level before,
                    // because 192/ 64, child_words is set to 3, and need will be set to 1. So a new "root"
                    // level will be created that has only 1 word

                    //create the new level, assign `need` many words to 0
                    //then iterate child level, and summarize
                    _levels[level].assign(need, 0);
                    const auto child_level = level - 1;

                    for (word_index j = 0; j < _levels[child_level].size(); ++j)
                        // if child is full, set corresponding parent bit to 1
                        if (get_word(child_level, j) == full)
                            set_word_bit(level, j / word_bits, j % word_bits);
                            // _levels[l][j / word_bits] |= (
                            //     word{1} << (j % word_bits));
                    _depth = level + 1;
                }
                child_words = need;
            }
            _capacity = slots;
        }

        [[nodiscard]] std::size_t capacity() const noexcept {
            return _capacity;
        }

        [[nodiscard]] word word_at(const word_index index) const {
            return get_word(LEAF_LEVEL, index);
        }


        // return the lowest free slot, or npos when full / past capacity
        [[nodiscard]] FORCE_INLINE slot_index descend() const noexcept {
            if (_depth == 0) return npos;

            std::size_t pos = 0;
            // depth is reversed. leaves are always at index 0
            for (std::size_t l = _depth; l-- > 0;) {
                // take qword at [level][position]
                const word word = get_word(l, pos);

                // extract first 0 bit on the word (this is a single instruction on x86)
                const int bit_pos = std::countr_one(word);

                // if pos = max (64 = 64), we're full, return nothing
                // only happens if root is full
                if (bit_pos == word_bits) return npos;

                // each level stores another power of 64 words.
                // root maps to 64 words, level 2 maps each 64 words to another 64 (4096), etc
                // so next pos = pos (0 at the start) * 64 + current_bit pos (e.g. 13)
                // next iteration at level 2 would be 13 * 64 + e.g. 48, meaning the 13th (l1) 48th (l2) word would be next.
                // see the diagram for more info
                pos = pos * word_bits + static_cast<std::size_t>(bit_pos);
            }

            //final position 0 - 64^6 is pos
            const slot_index slot = pos;

            assert(slot < max_items && "slotmap exceeded maximum values");

            // if we're out of slots, return nothing so the caller knows to call grow
            if (slot >= _capacity) return npos;

            return slot;
        }

        // mark slot occupied and propagate if necessary
        FORCE_INLINE
        slot_index occupy(const slot_index bit_slot) noexcept {
            const word_index word_index = bit_slot / word_bits;
            const slot_index bit_offset = bit_slot % word_bits;

            set_word_bit(LEAF_LEVEL, word_index, bit_offset);
            if (get_word(LEAF_LEVEL, word_index) == full) propagate_full_up(word_index);

            return bit_slot;
        }

        // TODO compiler agnostic inline
        // compiler should optimize the per fn divides away through CSE (hopefully)
        // also the div and rem are one op on x86

        FORCE_INLINE
        void set_word_bit(const std::size_t level, const word_index index, const slot_index bit_offset) noexcept {
            _levels[level][index] |=
                (word{1} << (bit_offset));
        }

        // in another universe i probably wouldve extracted (slot div bits, slot rem bits)
        // into a separate function that returns std pair but having had the insert path decimated
        // by a 3x slowdown because of std optional causing a store forwarding issue im not gonna risk it lol
        FORCE_INLINE
        void set_leaf_bit(slot_index slot) noexcept {
            // _levels[LEAF_LEVEL][slot / word_bits] |=
            //     (word{1} << (slot % word_bits));
            set_word_bit(LEAF_LEVEL, slot / word_bits, slot % word_bits);
        }

        FORCE_INLINE
        void clear_leaf_bit(slot_index slot) noexcept {
            clear_word_bit(LEAF_LEVEL, slot / word_bits, slot % word_bits);
        }

        FORCE_INLINE
        void clear_word_bit(const std::size_t level, const word_index index, const slot_index bit_offset) {
            _levels[level][index] &= ~(
                word{1} << (bit_offset)
                );
        }

        // [[nodiscard]] [[gnu::always_inline]]
        // std::size_t get_leaf_word_from_bit(bit_index slot) const noexcept {
        //     return _levels[LEAF_LEVEL][slot / word_bits];
        // }

        [[nodiscard]] FORCE_INLINE
        word get_word(const std::size_t level, const word_index word) const noexcept {
            return _levels[level][word];
        }

    private:
        // template <bool DENSE>
        // friend struct HierarchicalBitmapIterator;

        // propagate full status to parents
        void propagate_full_up(word_index child_word_index) noexcept {
            for (std::size_t level = 1; level < _depth; ++level) {
                const word_index parent_word_index =
                    child_word_index / word_bits;
                const slot_index parent_bit_offset = child_word_index % word_bits;

                // set parent to high on child bit
                // _levels[l][parent_word_index] |= (
                //     word{1} << (child_word_index % word_bits)
                // );
                set_word_bit(level, parent_word_index, parent_bit_offset);


                // if parent is not full, end
                if (get_word(level, parent_word_index) != full) break;
                child_word_index = parent_word_index;
            }
        }

        // propagate no longer full status up to parents
        void propagate_clear_up(word_index child_word_idx) noexcept {
            for (std::size_t level = 1; level < _depth; ++level) {
                const word_index parent_word_idx = child_word_idx / word_bits;
                const bool parent_was_full = (
                    get_word(level, parent_word_idx) == full
                );

                // AND self with only child bit undone
                // _levels[l][parent_word_idx] &= ~(
                //     word{1} << (child_word_idx % word_bits)
                // );
                clear_word_bit(level, parent_word_idx, child_word_idx % word_bits);

                // only continue if self was full (meaning parent marked full)
                if (!parent_was_full) break;
                child_word_idx = parent_word_idx;
            }
        }

        std::array<std::vector<word>, max_levels> _levels{};
        std::size_t _depth = 0;
        std::size_t _capacity = 0;
    };
}