module;
#include <cstdint>
#include <print>
export module main;
import slotmap;

struct TestStruct {
    uint32_t hello = 3;
    uint32_t world = 5;
};

void struct_demo() {
    inco::SparseSlotMap<TestStruct> slot_map;
    slot_map.emplace_back(21, 34);

    for (unsigned i = 0; i < 100; i++) {
        slot_map.emplace_back(i);
        slot_map.emplace_back(TestStruct{.hello = i * 21, .world = i});
    }

    slot_map | inco::unrolled([&](auto idx, auto& value) {
        std::println("extracted: hello:{} world:{}", value.hello, value.world);
    });
}

void int_demo() {
    inco::SparseSlotMap<std::uint32_t> slot_map;

    slot_map.reserve(192);

    slot_map.emplace_back(21);

    auto keys = std::vector<inco::Key<std::uint32_t>>();
    for (int i = 0; i < 100; i++) {
        keys.emplace_back(slot_map.emplace_back(i));
    }

    for (int i = 0; i <100; i +=2) {
        slot_map.erase(keys[i]);
    }

    for (const auto [_, value] : slot_map) {
        std::println("extracted: {}", value);
    }

    inco::unrolled(slot_map, [&](auto idx, auto& value) {
        std::println("extracted_unrolled: {}", value);
    });
}

extern "C++" int main() {
    int_demo();
    struct_demo();

    return 0;
}
