# slotmap

A very fast [slotmap](https://docs.rs/slotmap/latest/slotmap/) implementation written in C++23 using modules.

```cpp
import slotmap;

inco::SparseSlotMap<Mesh> meshes;
auto key = meshes.emplace_back(load("thing.obj"));

// Returns (stable) pointer to mesh
Mesh* mesh = meshes.find(key);
// Recycles the key, and increases the slot's generation
meshes.erase(key);
// Returns null, since this key's version is too low
Mesh* mesh2 = meshes.find(key);

for (auto&& [index, mesh] : meshes)
    draw(mesh);
    
// Or, for the fastest possible iteration
slot_map | inco::unrolled([&](auto idx, auto& mesh) {
    draw(mesh);
});
```

## Requirements

- GCC 16.2 or Clang 22.1. Earlier versions could potentially work. MSVC will not work due to usage of statement expressions.
- CMake 4.3+ with C++ module support.

## TODO

- Multithreading