# slotmap

[![CI](https://github.com/inconspicuoususername/slotmap/actions/workflows/ci.yml/badge.svg)](https://github.com/inconspicuoususername/slotmap/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg?logo=cplusplus)
![Compilers](https://img.shields.io/badge/compilers-GCC%2016%20%7C%20Clang%2022-orange.svg)
![Header](https://img.shields.io/badge/modules-C%2B%2B20-8A2BE2.svg)

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