# Tasks

## M1 leftovers
- [ ] Verify build on Windows (MSVC 2022, vcpkg SDL3).

## M3 follow-ups (not blocking)
- [ ] Per-device volk tables (VolkDeviceTable) if more than one Vulkan device per process is ever needed.
- [ ] HDR texture format (RGBA16F) instead of clamping RGBA16161616F to RGBA8.
- [ ] Async texture upload (transfer ring) instead of vkQueueWaitIdle per texture.

## Before first binary release
- [ ] licenses/ with SDL3 (zlib) and Dear ImGui (MIT) texts.

## Later (devui)
- [ ] platform event hook for devui input; panels: mounts/VPKs, cvars, console, interfaces, modules.

## M2 (in order)
1. [x] VPK v1/v2 directory parser (tree, archive index, preload bytes, `_dir` + `_NNN` parts) + synthetic test.
2. [x] Mount VPK in FileSystem search paths (`foo.vpk` -> `foo_dir.vpk`); lowercase lookup; verify against real HL2 (hl2_pak_dir.vpk).
3. [x] BSP v19/v20 header + lump table + bounds-checked lump access + test.
4. [x] BSP lumps for world: planes, vertices, edges, surfedges, faces, texinfo, texdata, models, entities (text).
5. [x] VTF header + mip/format table (DXT1/3/5, BGRA8888, BGR888, etc.) + test.
6. [x] VMT: shader + params, DX9/HDR fallback blocks, `cond?$param`, patch shader.
7. [x] MDL/VVD/VTX: LOD0 default body -> vertices + per-mesh triangles, materials, skins.
8. [x] BSP: nodes/leafs/visibility, displacements, game lump (static props), pakfile (zip) lump.

## M3 (in order)
0. [x] Vulkan sync/lifetime pass (validation + sync validation clean). [x] VMA.
1. [x] render::2d + render::Device API; Vulkan backend (swapchain + headless offscreen); RGBA8 textures.
2. [x] VTF -> render::TextureData (materials/texture) -> render::Texture: BC1-3 native when caps.textureCompressionBC, CPU decode otherwise; mip chains; sampler addressing from VTF flags. (DXT1/3/5, BGR(A)888x, RGBA16F, UV88); material -> texture binding.
3. [ ] World mesh builder: faces -> polygons, displacement grids, lightmap atlas; LightmappedGeneric/WorldVertexTransition basics.
4. [ ] Static props via studio loader; skybox (sky shader, 2D skybox camera later).
5. [ ] PVS/frustum culling; `map <name>` command mounts pakfile, loads BSP, renders.
6. [x] devui draws through render::2d.
