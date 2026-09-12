# Project review — 2026-09-11

Scope: local source, project records, Codegraph, and the configured macOS Debug build. No Valve implementation code was used.

## Architecture observed

- Startup: `engine/main.cpp` mounts gameinfo search paths, creates console/window/device, executes configs and commands, then runs a free-camera rendering loop.
- Content: `filesystem/` supplies loose/VPK/map ZIP data; `formats/` parses bytes independently of GPU and filesystem state.
- Rendering: `world::World::load` builds BSP geometry and resolves materials/entities/props; `World::draw` submits through the backend-neutral `render::Device`. Vulkan owns backend resources.
- Compatibility: `compat/interfaces.cpp` implements a name-to-pointer registry and module factory lookup. It does not implement the engine interfaces required by a retail client/server.
- Simulation: `Clock::advance` computes ticks, but `main` discards the count. Brush entities and props are render placements, not a gameplay entity runtime.

Codegraph initialized locally: 75 indexed files, 977 nodes, 2726 edges. Generated database files are ignored by `.codegraph/.gitignore`. Graph queries provide navigation evidence; inferred edges and test coverage still require checking against source and CMake.

## Priorities

1. Next bounded M3 slice: translucent sorting, already listed in TASKS.md. Inspect world faces, brush entities and props together; validate overlapping transparency across all three. Sorting by material or sorting each object category separately is insufficient. Document any remaining intersecting-surface limitation.
2. Before M4: pin the first HL2 game build, module platform/architecture and a usable test host. The local client/server dylibs were inspected and are i386 Mach-O. They cannot supply the retail module test path for this arm64 macOS development build.
3. Establish module startup and required versioned interface contracts, then connect fixed ticks to the game runtime. The existing synthetic module test proves factory plumbing only.
4. Track HL2 Episodes, TF2, Portal, Portal 2 and Counter-Strike: Source independently. SDK 2013 is the primary reference, not evidence that any target's exact ABI or behavior is supported.

## Documentation corrected

README previously claimed no VPK support and only window startup. It now describes the implemented map renderer, explicitly retains the no-gameplay limitation, and records the requested game targets.

## Limits

Validation: macOS Debug configure/build succeeded; all 23 configured CTest tests passed (97.79 seconds), including real HL2 content tests, Vulkan render/swapchain tests, module factory test and smoke test. This run used ANVIL_DEVUI=OFF and did not explicitly enable Vulkan validation layers.

This is an initial architecture review, not an exhaustive parser/security audit or a pixel comparison with Source. Existing compatibility notes remain authoritative for approximations and missing features. Windows/Linux were not revalidated in this review.
