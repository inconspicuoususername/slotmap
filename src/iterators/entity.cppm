module;
#include <cstddef>
export module slotmap.iterators:entity;

namespace slotmap {
    export template <class T>
    struct SlotMapIteratorEntry {
        std::size_t index;
        T& value;
    };
}