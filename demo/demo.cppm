module;
#include <cstdint>
#include <print>
export module main;
import slotmap;

extern "C++" int main() {
    slotmap::SparseSlotMap<std::uint32_t> slot_map;

    slot_map.reserve(192);

    slot_map.try_emplace(21);

    for (int i = 0; i < 100; i++) {
        slot_map.try_emplace(i);
    }

    for (const auto [_, value] : slot_map) {
        std::println("extracted: {}", value);
    }
    
    return 0;
}