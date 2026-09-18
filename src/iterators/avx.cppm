module;
#include <immintrin.h>

#include <array>
#include <bit>
#include <cstdint>
#include <type_traits>
export module slotmap:iterators.avx;
import :concepts;
import :utils;

namespace inco {
    // did claude cook?
    // update: no
    // code was really confusing so i had gemini annotate the avx slop
    // enjoy
    export template <class Finder, class Store, class Fn>
        requires LiveBitmapView<Finder>
    void for_each_avx(const Finder& bm, Store& store, Fn&& fn) {
        using T = std::remove_pointer_t<decltype(store.at(0))>;
        using Byte = std::conditional_t<std::is_const_v<T>, const char, char>;
        static_assert(Finder::word_bits == 64);
        static_assert(
            std::has_single_bit(sizeof(T)) && sizeof(T) * 63 <= 0xFFFF);

        // Calculates how much we need to shift an index to get a byte offset.
        // e.g., if T is 4 bytes (uint32_t), countr_zero(4) is 2.
        // Shifting left by 2 is the same as multiplying by 4.
        constexpr unsigned shift_amount = std::countr_zero(sizeof(T));

        // Create an array containing [0, 1, 2, ..., 63] at compile time
        static constexpr auto sequence_0_to_63 = [] {
            std::array<std::uint8_t, 64> a{};
            for (unsigned i = 0; i < 64; ++i)
                a[i] = static_cast<std::uint8_t>(i);
            return a;
        }();

        // Load that [0...63] sequence into a 512-bit vector register.
        // Each of the 64 bytes in this vector holds its own index.
        const __m512i vec_indices = _mm512_loadu_si512(sequence_0_to_63.data());

        const std::size_t total_words =
            utils::ceil_div(bm.capacity(), std::size_t{64});

        for (std::size_t cap_i = 0; cap_i < total_words; ++cap_i) {
            const std::uint64_t word = bm.word_at(cap_i);
            if (!word) continue;

            const std::size_t global_offset = cap_i * 64;
            auto* raw_bytes = reinterpret_cast<Byte*>(store.at(global_offset));

            // THE MAGIC INSTRUCTION:
            // Takes the 64 bits from 'word'. Wherever a bit is 1, it takes the
            // corresponding byte from 'vec_indices' and squashes them together
            // at the beginning of the output vector 'vec_compressed'.
            // If word has bits 2 and 5 set, vec_compressed starts with [2, 5, 0, 0...]
            const __m512i vec_compressed = _mm512_maskz_compress_epi8(
                word,
                vec_indices);

            // Convert the 8-bit indices into 16-bit byte-offsets (index * sizeof(T)).
            // We do this in two halves (lo and hi) because widening 64 8-bit integers
            // results in 64 16-bit integers, which requires two 512-bit registers.
            const __m512i vec_offsets_lo = _mm512_slli_epi16(
                _mm512_cvtepu8_epi16(_mm512_castsi512_si256(vec_compressed)),
                shift_amount);

            const __m512i vec_offsets_hi = _mm512_slli_epi16(
                _mm512_cvtepu8_epi16(
                    _mm512_extracti64x4_epi64(vec_compressed, 1)),
                shift_amount);

            // Store the pre-calculated offsets into a standard array
            alignas(64) std::uint16_t offsets_array[64];
            _mm512_store_si512(offsets_array, vec_offsets_lo);
            _mm512_store_si512(offsets_array + 32, vec_offsets_hi);

            // Process the offsets block using a standard scalar unrolled loop
            const int set_bits_count = std::popcount(word);
            int i = 0;

            for (; i + 8 <= set_bits_count; i += 8) {
#pragma GCC unroll 8
                for (int k = 0; k < 8; ++k) {
                    const std::uint16_t byte_offset = offsets_array[i + k];
                    // byte_offset >> shift_amount recovers the original bit index
                    fn(global_offset + (byte_offset >> shift_amount),
                       *reinterpret_cast<T*>(raw_bytes + byte_offset));
                }
            }

            // Handle any remaining bits (tail loop)
            for (; i < set_bits_count; ++i) {
                const std::uint16_t byte_offset = offsets_array[i];
                fn(global_offset + (byte_offset >> shift_amount),
                   *reinterpret_cast<T*>(raw_bytes + byte_offset));
            }
        }
    }

    export template <class Finder, class Store, class Fn>
        requires LiveBitmapView<Finder>
    void for_each_avx_lanes(const Finder& bm, Store& store, Fn&& fn) {
        using T = std::remove_pointer_t<decltype(store.at(0))>;
        static_assert(Finder::word_bits == 64);
        static_assert(
            std::has_single_bit(sizeof(T)) && sizeof(T) * 63 <= 0xFFFF);

        static constexpr auto sequence_0_to_63 = [] {
            std::array<std::uint8_t, 64> a{};
            for (unsigned i = 0; i < 64; ++i)
                a[i] = static_cast<std::uint8_t>(i);
            return a;
        }();

        const __m512i vec_indices = _mm512_loadu_si512(sequence_0_to_63.data());

        const std::size_t total_words =
            utils::ceil_div(bm.capacity(), std::size_t{64});

        for (std::size_t cap_i = 0; cap_i < total_words; ++cap_i) {
            const std::uint64_t word = bm.word_at(cap_i);
            if (!word) continue;

            const std::size_t global_offset = cap_i * 64;
            auto* base_pointer = store.at(global_offset);

            // Same compression as before: all active bit indices are squashed
            // to the front of this vector, packed as contiguous 8-bit values.
            __m512i vec_compressed = _mm512_maskz_compress_epi8(
                word,
                vec_indices);

            int remaining_bits = std::popcount(word);

            for (; remaining_bits >= 8; remaining_bits -= 8) {
                // Extract the lowest 64-bits from the 512-bit register into a standard
                // CPU register. Since each index is 1 byte, 64 bits holds exactly 8 indices.
                std::uint64_t current_8_indices = static_cast<std::uint64_t>(
                    _mm_cvtsi128_si64(_mm512_castsi512_si128(vec_compressed)));

                // Shift the entire 512-bit vector down by 64 bits (8 bytes).
                // This moves the NEXT 8 indices into the lowest position for the next iteration.
                vec_compressed = _mm512_alignr_epi64(
                    vec_compressed,
                    vec_compressed,
                    1);

#pragma GCC unroll 8
                for (int k = 0; k < 8; ++k) {
                    // Mask out the lowest byte to get the actual index (0-63)
                    const std::size_t bit_index = current_8_indices & 0xFF;

                    // Shift right by 8 bits to expose the next index for the next unrolled step
                    current_8_indices >>= 8;

                    fn(global_offset + static_cast<std::size_t>(bit_index),
                       base_pointer[bit_index]);
                }
            }

            // Tail loop: process remaining 1 to 7 bits
            std::uint64_t current_8_indices = static_cast<std::uint64_t>(
                _mm_cvtsi128_si64(_mm512_castsi512_si128(vec_compressed)));

            for (; remaining_bits > 0; --remaining_bits) {
                const std::size_t bit_index = current_8_indices & 0xFF;
                current_8_indices >>= 8;
                fn(global_offset + bit_index, base_pointer[bit_index]);
            }
        }
    }
}