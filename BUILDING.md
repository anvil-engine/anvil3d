# Building

Requires CMake 3.20+, a C++20 compiler (MSVC 2022, GCC 11+, Clang 14+), and SDL3 development files.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Getting SDL3:
- Linux: distro package (`libsdl3-dev`, `SDL3-devel`) or build from source.
- Windows: `vcpkg install sdl3`, then configure with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.
- macOS: `brew install sdl3`.

If CMake cannot find SDL3, pass `-DCMAKE_PREFIX_PATH=<sdl3 install dir>`.

Options:
- `-DANVIL_DEVUI=ON` builds the developer overlay (Dear ImGui, downloaded at configure time). Run with `-devui`.
- `-DANVIL_HL2_DIR=<Half-Life 2 install>` enables tests over your own game files.
