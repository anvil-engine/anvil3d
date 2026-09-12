# Architecture

```
Source game DLLs (client/server)
  -> Source-compatible interfaces (interface registry, versioned names)   [compat/, M1+]
  -> anvil engine systems (fs, materials, models, bsp, audio, physics, vgui)
  -> platform (SDL3) / render backends (vulkan, d3d11, gles)
```

## Directories (grow only when code exists)
- common/   logging, cmdline, small utils. No deps.
- platform/ OS + window + input + dynamic modules. Only place SDL is included.
- filesystem/ virtual FS, search paths, gameinfo.txt. OS paths stop here.
- formats/  pure parsers over bytes (bsp, vtf, vmt, studio). No I/O. Validate all cross-refs at load. Use common/bytes.h readAt for new binary readers.
- render/   backend-neutral API: Device, textures, Batch2D (render::2d). render/vulkan/: the only backend so far (all Vulkan types stay there).
- materials/ CPU-side material system: VTF -> render::TextureData (BC decode/convert). No backend knowledge.
- world/    BSP -> world::Mesh (geometry, lightmap atlas; pure CPU) and World (map load, pakfile mount, materials, draw via render::Device).
- devui/    Dear ImGui developer overlay (optional, ANVIL_DEVUI). Internal only; not VGUI. Draws via render::2d.
- physics/  Jolt Runtime + Scene; bodies, virtual player, raycasts. Public API uses map units/Z-up; Jolt types and metre conversion stay in physics.cpp.
- world/collision: BSP brush hull construction, terrain and initial static prop collision; independent of render visibility.
- vgui/     Original resources/localization, menu data and scheme interpretation (color aliases, ordered font variants/ranges and custom font paths). Font rasterization, panel rendering and VGUI ABI are not implemented.
- gameplay/ Independent combat/menu test rig, only via -diagnosticplay. Not Source game behavior.
- compat/   Source-facing ABI: interface registry, CreateInterface. Later: engine interfaces for game DLLs.
- engine/   entry point, console/cvars, clock, main loop.
- tests/    unit tests (synthetic data only), tests/data/testgame (smoke test game dir).

Libraries: anvil_common <- anvil_platform, anvil_filesystem, anvil_formats, anvil_render (+vulkan, volk, vma), anvil_materials, anvil_world, anvil_devui <- anvil_engine <- anvil (exe), tests.
World pipeline: formats/bsp -> world::buildMesh (CPU mesh + atlas) -> render::Device::createMesh/createTexture -> draw3d.
Planned: render/d3d11, render/gles, audio/, vgui/.

## Rules
- Parsers own raw formats; renderer never reads raw BSP/VTF.
- Internal paths are virtual (`materials/foo.vmt`), OS paths only in platform/ + filesystem mounts.
- Libraries are static; `anvil` links them. anvil-owned module boundaries use C ABI.
- ABI-sensitive code (Source interfaces called by game DLLs) documents: platform, compiler, calling convention, layout, ownership.

## Startup (current)
cmdline -> log level -> mount game (gameinfo.txt) -> console -> Vulkan library probe -> window -> render::Device -> devui -> anvil.cfg, +commands (e.g. +map) -> loop (events, mouse/WASD, clock -> fixed Jolt ticks or noclip, beginFrame/world draw3d/draw2d/endFrame, fps_max) -> shutdown (world, device, window).
