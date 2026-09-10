# Status

Version: 0.10.0 (dev)
Milestone: M3 Renderer — in progress. First visual milestone reached: `+map d1_trainstation_01` loads the BSP, mounts its pakfile, draws world geometry with base textures and lightmaps; free camera. Next: sky, brush entities, culling, static props.

## Working
- Build verified: macOS arm64 (AppleClang 21), Linux arm64 (Ubuntu 25.04, GCC 14, via podman). Windows unverified.
- common/: logging, Source cmdline, KeyValues text parser (conditionals, depth limit).
- filesystem/: search paths with path IDs, path normalization + traversal rejection, case-insensitive lookup on Linux, gameinfo.txt parsing + mounting (dirs, `dir/*`).
- platform/: SDL3 window, dynamic module loading (SDL_LoadObject).
- compat/: engine interface registry + CreateInterface factory ABI.
- engine/: console (cvars, commands, exec, quit, echo, cvarlist), Clock (fixed tick, pause, timescale), fps_max loop.
- filesystem/vpk: VPK v1/v2 tree, parts + embedded data, preload, CRC (warn-only), bounds-checked; mounted from gameinfo.
- Real HL2 install: 6 VPKs + 5 dirs mount; all 36,015 VPK entries (3.5 GB) read OK (`-DANVIL_HL2_DIR=...` enables test vpk_hl2).
- formats/bsp: VBSP v19/v20: world lumps, nodes, leafs (v0/v1), leaffaces, PVS (validated RLE), dispinfo+dispverts, static props (sprp v4-6), pakfile; findLeaf(), pvs(). All cross-refs validated at load. 78/78 non-empty HL2 maps load (`bsp_hl2`: 18545 static props, 10879 displacements).
- filesystem/zip + Archive interface: map pakfile mounts in front (FileSystem::addArchive/removeArchive). 21786 pakfile materials resolve.
- formats/vtf: VTF 7.0-7.5 header, resources, cubemap faces, per mip/frame/face/slice image lookup, exact size validation. 5237/5237 HL2 textures parse (`vtf_hl2`). No pixel decoding yet.
- formats/vmt: resolved material (shader, flat params, proxies) for dxlevel 95 + HDR; patch includes. 5304 HL2 materials resolve (`vmt_hl2`; 1 retail-broken dx60 file skipped).
- formats/studio: MDL v44-48 + VVD v4 (fixups) + VTX v7 (trilists/tristrips) -> LOD0 default body; checksums cross-checked. 2171/2171 HL2 models load (`studio_hl2`). No skeleton/anim/flex/bodygroups yet.
- devui/: optional Dear ImGui overlay (`-DANVIL_DEVUI=ON`, run with `-devui`), drawn via render::2d; ImGui textures via RendererHasTextures.
- render/: backend-neutral Device (textures, frames, draw2d, caps, headless readPixels). render/vulkan: MoltenVK/native via volk, portability enumeration/subset, swapchain resize/out-of-date, deferred destruction, 2D pipeline. Verified: Apple M4 (MoltenVK, Vulkan 1.1.323) windowed + headless; llvmpipe (Linux) headless under Khronos validation: no messages.
- materials/texture: VTF -> render::TextureData (mips, BC1-3 kept or decoded, other formats -> RGBA8). All 5237 HL2 textures convert on both paths (`vtf_hl2`).
- render: TextureFormat RGBA8/BC1/BC2/BC3, mip chains, clamp/repeat samplers; DeviceOptions::forceUncompressedTextures. VMA for all Vulkan memory.
- render: 3D path — static meshes (Vertex3D: pos, uv, lightmap uv), Draw3D = texture * lightmap * scale (Opaque/AlphaTest/Translucent), depth buffer (reverse Z), column-major viewProj with y-up clip space.
- world/: BSP -> world::Mesh (fan-triangulated faces, displacement grids, texture + lightmap UVs, shelf-packed RGBA8 lightmap atlas), grouped per material. World: loads maps/<name>.bsp, mounts pakfile {GAME,BSP} at head, resolves VMT -> VTF -> device textures (missing = magenta checker), LightmappedGeneric-style drawing. All 78 HL2 maps build (CPU); d1_trainstation_01 renders headless and windowed (`world_hl2`, 0 missing assets).
- world/visibility: per-frame PVS (camera leaf -> cluster -> decompressed PVS) + frustum (face bounds vs 5 planes) -> visible faces merged into per-material index ranges. Displacements (never in leaf face lists in HL2) get clusters from their bounds pushed down the node tree. Counters: faces / PVS / frustum / submitted / triangles / draws (`r_worldstats`); `r_novis 1` = frustum only. d1_trainstation_01 spawn: 6678 faces -> 1352 PVS -> 126..643 submitted over 4 view directions; images identical with PVS on/off (`world_hl2`).
- engine: `map <name>` command; `anvil.cfg` + `+commands` run after renderer init; free camera (RMB look, WASD, Space/Ctrl, Shift), `sensitivity` cvar; platform input (keys by SDL name, mouse buttons, relative motion).
- engine: renders every frame (clear + world + devui); flags -norender, -novsync, -vkdebug.
- Tests: cmdline, keyvalues, filesystem, console(+clock), interfaces (real module load), smoke (tests/data/testgame).

## Stubbed / not implemented
- gameinfo implicit rules (auto gamebin, _<language> dirs, low-violence game_lv) not applied.
- cvar flags (cheat/archive), config.cfg saving, autocomplete, in-game console UI.
- Simulation ticks computed but nothing consumes them.
- World render gaps (logged STUB/PARTIAL at runtime): Water/Refract surfaces not drawn; WorldVertexTransition draws $basetexture only; other shaders drawn as LightmappedGeneric; no sky; brush entities (models *1..) not drawn; static props, decals, overlays not drawn; lightmap style 0 + flat samples only (no bump, no switchable lights); translucent draws unsorted; RGBA16161616F textures clamped to RGBA8 (WARN).

## Known broken
- none
