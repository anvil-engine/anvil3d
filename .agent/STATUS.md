# Status

Version: 0.10.0 (dev)
Milestone: M3 Renderer — in progress. Device/2D/Vulkan, sync pass, VMA, VTF textures (BC native + CPU fallback) done; next world mesh.

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
- engine: renders every frame (clear + devui); flags -norender, -novsync, -vkdebug.
- Tests: cmdline, keyvalues, filesystem, console(+clock), interfaces (real module load), smoke (tests/data/testgame).

## Stubbed / not implemented
- gameinfo implicit rules (auto gamebin, _<language> dirs, low-violence game_lv) not applied.
- cvar flags (cheat/archive), config.cfg saving, autocomplete, in-game console UI.
- Simulation ticks computed but nothing consumes them.

## Known broken
- none
