# Agent rules

Read first: STATUS.md, BLOCKERS.md, TASKS.md. Then only files relevant to the task.

## Hard rules
- Clean-room. Never copy/port/decompile Valve Source Engine code. Public SDK headers, docs, observed formats only. Unsure -> independent design.
- Never commit game assets, Valve binaries, build dirs, local absolute paths.
- Game data = untrusted input. Bounds-check every binary read.
- HL2 compatibility beats new features and pretty architecture.
- Stubs must be labeled (STUB/PARTIAL in COMPATIBILITY.md, WARN log at runtime). Never fake silently.

## Workflow
1. Pick next item from TASKS.md. Smallest vertical slice.
2. Implement, build, test (commands below).
3. Update STATUS.md / TASKS.md / DECISIONS.md / COMPATIBILITY.md only where changed.
4. Report: `DONE: / NEXT: / BLOCKED:`. Build failure: `ERROR: / CAUSE: / FIX:`. No essays.

## Build / test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```
Render tests need a Vulkan implementation (Mac: brew molten-vk; Linux CI: mesa-vulkan-drivers = llvmpipe); `render` test exits 77 (skipped) without one. `ANVIL_VK_DEBUG=1` enables validation layers in tests.
Real-data tests (not in repo): add `-DANVIL_HL2_DIR="<Half-Life 2 install>"` -> vpk/bsp/vtf/vmt/studio `_hl2` tests.
Linux check from the Mac (GCC catches what AppleClang misses; run before committing new files):
```
podman machine start; podman run --rm -v "$PWD":/src:ro docker.io/library/ubuntu:25.04 bash -c 'apt-get update -qq && apt-get install -y -qq cmake ninja-build g++ libsdl3-dev glslang-tools libvulkan1 mesa-vulkan-drivers >/dev/null && cmake -S /src -B /tmp/b -G Ninja && cmake --build /tmp/b && ctest --test-dir /tmp/b'; podman machine stop
```

## Code
- C++20, RAII, explicit ownership, no singletons/global state except logging and the interface registry.
- SDL only inside platform/. Backend APIs only inside renderer/<backend>/.
- Log with ANVIL_DEBUG/INFO/WARN/ERROR(subsystem, fmt, ...) from common/log.h.
- Comments: why, compat assumptions, ABI, ownership. Not what.
- Every binary format parser gets a test in tests/ using synthetic data.
- `ponytail:` comment marks a deliberate shortcut + upgrade path.
