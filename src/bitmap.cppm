module;

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <cassert>
#include <cmath>

export module slotmap:bitmap;
import :utils;

namespace slotmap {
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
        static constexpr int word_bits = 64;
        static constexpr word full = ~word{0};
        static constexpr std::size_t max_levels = 6; // 64^6 > 2^32, cca 68 bil
        static constexpr std::size_t max_items =
            ic::pow(word_bits, max_levels);

        static constexpr std::size_t LEAF_LEVEL = 0;

        // take lowest free slot, or return nullopt when out of capacity
        // (the caller should grow() and retry).
        [[nodiscard]] std::optional<std::size_t> acquire() noexcept {
            if (_depth == 0) return std::nullopt;

            std::size_t pos = 0;
            // depth is reversed. leaves are always at index 0
            for (std::size_t l = _depth; l-- > 0;) {
                // take qword at [level][position]
                const word word = _levels[l][pos];

                // extract first 0 bit on the word (this is a single instruction on x86)
                const int bit_pos = std::countr_one(word);

                // if pos = max (64 = 64), we're full, return nothing
                // only happens if root is full
                if (bit_pos == word_bits) return std::nullopt;

                // each level stores another power of 64 words.
                // root maps to 64 words, level 2 maps each 64 words to another 64 (4096), etc
                // so next pos = pos (0 at the start) * 64 + current_bit pos (e.g. 13)
                // next iteration at level 2 would be 13 * 64 + e.g. 48, meaning the 13th (l1) 48th (l2) word would be next.
                // see the diagram for more info
                pos = pos * word_bits + static_cast<std::size_t>(bit_pos);
            }

            //final position 0 - 64^6 is pos
            const std::size_t slot = pos;

            assert(slot < max_items && "slotmap exceeded maximum values");

            // if we're out of slots, return nothing so the caller knows to call grow
            if (slot >= _capacity) return std::nullopt;

            // extract word index out of exact bit pos
            // iirc on x86 the div and rem optimizes down to one instruction
            const std::size_t lw = slot / word_bits;
            // set word + bit combo to occupied
            _levels[LEAF_LEVEL][lw] |= (word{1} << (slot % word_bits));

            //if current level is now full, propagate summaries up to parents
            if (_levels[LEAF_LEVEL][lw] == full) propagate_full_up(lw);
            return slot;
        }

        void release(std::size_t slot) noexcept {
            const std::size_t lw = slot / word_bits;
            const bool was_full = (_levels[0][lw] == full);
            _levels[LEAF_LEVEL][lw] &= ~(word{1} << (slot % word_bits));
            // if the word was previously full, the parent bit was set, so propagate
            if (was_full) propagate_clear_up(lw);
        }

        // grow so at least `slots` indices are representable
        void grow(const std::size_t slots) {
            if (slots <= _capacity) return;

            //convert total slots into words. ceil div because any overflow must be a new word
            // rather than the default floor of integer division
            const std::size_t leaf_words = ic::ceil_div(slots, word_bits);

            if (_depth == 0) _depth = 1;
            // new leaf words should be initialized to 0
            _levels[LEAF_LEVEL].resize(leaf_words, 0);

            //now go through each level and update
            std::size_t child_words = leaf_words;
            for (std::size_t l = 1; child_words > 1; ++l) {
                // e.g. 3 words for 192 requested slots
                const std::size_t need = ic::ceil_div(child_words, word_bits);
                if (l < _depth) {
                    // if we're still in the already claimed levels, just expand it so that
                    // it fits the new per-level need
                    _levels[l].resize(need, 0);
                } else {
                    // if we're past the current depth, it means that we cannot satsify the summarization
                    // with our current levels. For example, if we only had one level before,
                    // because 192/ 64, child_words is set to 3, and need will be set to 1. So a new "root"
                    // level will be created that has only 1 word

                    //create the new level, assign `need` many words to 0
                    //then iterate child level, and summarize
                    _levels[l].assign(need, 0);
                    for (std::size_t j = 0; j < _levels[l - 1].size(); ++j)
                        if (_levels[l - 1][j] == full)
                            _levels[l][j / word_bits] |= (
                                word{1} << (j % word_bits));
                    _depth = l + 1;
                }
                child_words = need;
            }
            _capacity = slots;
        }

        [[nodiscard]] std::size_t capacity() const noexcept {
            return _capacity;
        }

        [[nodiscard]] word word_at(const std::size_t index) const {
            return _levels[LEAF_LEVEL][index];
        }

    private:
        // template <bool DENSE>
        // friend struct HierarchicalBitmapIterator;

        // propagate full status to parents
        void propagate_full_up(std::size_t child_word_index) noexcept {
            for (std::size_t l = 1; l < _depth; ++l) {
                const std::size_t parent_word_index =
                    child_word_index / word_bits;

                // set parent to high on child bit
                _levels[l][parent_word_index] |= (
                    word{1} << (child_word_index % word_bits)
                );

                // if parent is not full, end
                if (_levels[l][parent_word_index] != full) break;
                child_word_index = parent_word_index;
            }
        }

        // propagate no longer full status up to parents
        void propagate_clear_up(std::size_t child_word_idx) noexcept {
            for (std::size_t l = 1; l < _depth; ++l) {
                const std::size_t parent_word_idx = child_word_idx / word_bits;
                const bool parent_was_full = (
                    _levels[l][parent_word_idx] == full

                );

                // AND self with only child bit undone
                _levels[l][parent_word_idx] &= ~(
                    word{1} << (child_word_idx % word_bits)
                );

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