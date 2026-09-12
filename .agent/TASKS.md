# Tasks

## Active direction: execute original Source content (2026-09-12)
User direction supersedes the earlier independent combat/menu prototype. Do not recreate game menus, HUDs, animations, sounds or map progression. Renderer implementation may be modern and independent. Diagnostic replacements must be opt-in and explicitly identified.

- [x] Jolt Runtime/Scene, validated BSP brushes and initial walking/collision foundation (PARTIAL movement semantics).
- [x] Isolate independent combat/menu/HUD/targets behind `-diagnosticplay`; default content inspection spawns none of these.
- [x] Original VGUI resource loading through ordered VFS, including PLATFORM; relative #base/#include, UTF-16 localization and opt-in KeyValues escapes.
- [x] Interpret GameMenu.res labels, commands and observed visibility/order flags from original data. Console diagnostics only.
- [x] Interpret scheme color aliases and font candidate ranges/order, preserving original font attributes and custom font file paths. Font face loading, rasterization and proportional sizing remain next steps.
- [x] Load original custom TTF/OpenType faces through VFS and rasterize glyph coverage/metrics using authored scheme candidate order. Missing/system fonts and unsupported effects remain explicit.
- [x] Compose bounded single-line UTF-8 runs from original glyphs into RGBA8 text textures and backend-neutral 2D draw batches.
- [x] Interpret original panel descriptors: control identity/type, localized text, commands, state, tab order and observed relative geometry forms.
- [x] Instantiate common original panel controls with focus traversal, hit activation and explicit unsupported-type reporting.
- [x] Resolve original Scheme border aliases/layers and build scheme-derived paint plans for common controls.
- [x] Batch and submit original Scheme fills/borders through render::2d; verify headless pixels.
- [x] Resolve exact authored system-font families and upload clipped/aligned original panel text runs.
- [x] Drive common panels from SDL key/mouse edges and execute the small supported authored-command set; warn on every unsupported command.
- [ ] Instantiate/render original VGUI panels, schemes and fonts; execute supported menu commands through real mechanisms, report unsupported commands.
- [ ] Load weapon scripts and original viewmodel sequences/animations; independent diagnostic weapons do not satisfy Source weapon compatibility.
  - [x] Load bounded original weapon manifest/WeaponData fields through VFS and verify all 18 installed HL2 scripts.
  - [x] Parse local MDL sequence/activity metadata and resolve it for 17 installed HL2 viewmodels.
  - [x] Parse bounded MDL bind-pose bone names, parents, positions and quaternions.
- [ ] Execute authored skeletal animation; model poses/recoil must not be substituted on the game path.
- [ ] Instantiate authored entities and generic delayed I/O (no map-specific event chains).
- [ ] Original sound resources/scripts and audio playback.
- [ ] PHY collision, entity physics/parenting, Source movement semantics, triggers and progression driven by map data.
- [ ] Resolve game module/ABI path for original game-specific behavior; the retail mac i386 modules cannot load into this arm64 runtime.
- [ ] Modern renderer features remain a separate track; named algorithms require actual implementations and configuration.

## M1 leftovers
- [ ] Verify build on Windows (MSVC 2022, vcpkg SDL3).

## M3 follow-ups (not blocking)
- [ ] Per-device volk tables (VolkDeviceTable) if more than one Vulkan device per process is ever needed.
- [ ] HDR texture format (RGBA16F) instead of clamping RGBA16161616F to RGBA8.
- [ ] Async texture upload (transfer ring) instead of vkQueueWaitIdle per texture/mesh.
- [ ] Verify ARGB8888/RGB565/BGRX5551/BGRA5551/BGRA4444 channel order against real VTF files (tests pin assumed D3D layouts only).
- [ ] Backface culling (currently off).

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
3. [x] World mesh builder: faces -> polygons, displacement grids, lightmap atlas; LightmappedGeneric basics; `map <name>`; free camera.
3a. [x] WorldVertexTransition blend ($basetexture2 by displacement alpha). [ ] UnlitTwoTexture; translucent sorting.
3b. [x] Brush entities (models *N) placed by entity origin/angles (static). [x] 2D sky. [ ] sky_camera 3D skybox. [ ] render targets (_rt_camera).
4. [x] Static props via studio loader (ambient-only lighting). [ ] Prop lighting: direct light / VHV per-vertex lighting; LODs.
5. [x] PVS/frustum culling (per face; no areaportals, no occlusion).
6. [ ] Lightmap styles / bumped lightmaps; HDR lighting lump path with tonemapping.
6. [x] devui draws through render::2d.
