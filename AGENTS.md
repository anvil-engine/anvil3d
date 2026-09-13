# Repository Guidelines

## Project Structure & Module Organization

Anvil3D is a C++20 engine built from small static libraries. `engine/` contains the executable and console loop; `common/` provides shared parsing and logging; `filesystem/` handles search paths, VPK, ZIP, and `gameinfo.txt`. Source formats live in `formats/` (`bsp`, `vtf`, `vmt`, `studio`, and weapon data). Rendering is split between the backend-neutral API in `render/` and Vulkan code in `render/vulkan/`. `world/` builds maps, entities, visibility, materials, and collision; `physics/` wraps Jolt; `vgui/` reads original UI resources. Tests and small fixtures are in `tests/`; project planning and compatibility notes are in `.agent/`.

## Build, Test, and Development Commands

Configure a debug build with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
```

Run the complete local suite with `ctest --test-dir build --output-on-failure`. Run a focused test while iterating, for example `ctest --test-dir build -R 'studio|world' --output-on-failure`. Launch a mounted game install with `./build/anvil -basedir "/path/to/Half-Life 2" -game hl2 +map d1_trainstation_01`; use `-norender` for headless checks. Optional real-install tests are enabled by configuring `-DANVIL_HL2_DIR=/path/to/Half-Life\ 2`.

## Coding Style & Naming Conventions

Use C++20, two-space indentation, braces consistent with nearby code, and `-Wall -Wextra -Wpedantic` clean builds. Types and functions use `PascalCase` and `camelCase`; members end in `_`; constants use `kPascalCase`. Keep parsers bounds-checked, dependencies minimal, and comments limited to non-obvious reasons. Follow existing module boundaries instead of adding wrappers or speculative abstractions.

## Testing Guidelines

Tests are standalone CTest executables named `test_<area>` (for example, `tests/test_studio.cpp`). Add focused regression coverage for format and runtime behavior, including malformed-input cases when relevant. Do not require retail assets in ordinary tests; use bounded fixtures, with HL2 tests explicitly gated by `ANVIL_HL2_DIR`.

## Commit & Pull Request Guidelines

Use short imperative commit subjects, such as `Support prop_dynamic animations`. Keep commits focused and exclude build outputs, installed assets, and local paths. Pull requests should explain the compatibility behavior changed, identify validation commands, and call out any remaining `PARTIAL` or `UNSUPPORTED` behavior. Include screenshots only when a visual rendering change is part of the change.
