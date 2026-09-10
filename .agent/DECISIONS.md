# Decisions

DECISION: MIT license.
REASON: Permissive, per project brief.
IMPACT: Dependencies must be MIT/BSD/zlib/Apache-compatible.

DECISION: CMake >= 3.20, C++20, Ninja recommended.
REASON: Portable; C++20 available on MSVC 2022, GCC 11+, Clang 14+.
IMPACT: No compiler extensions (CMAKE_CXX_EXTENSIONS OFF).

DECISION: SDL3 for window/input/events, found via find_package (not vendored).
REASON: Mature, zlib license, covers Windows/Linux/Android, Vulkan/GL/GLES surface creation.
IMPACT: Users install SDL3 dev package or pass CMAKE_PREFIX_PATH. Linked PRIVATE to anvil_platform.

DECISION: Jolt Physics for M5 (not integrated yet).
REASON: MIT, deterministic option, character controller, constraints, raycasts, used in shipped games.
IMPACT: Wrapped behind anvil physics interfaces; game never sees Jolt types.

DECISION: Command line follows Source semantics (case-insensitive `-parm value`, `+cmd args`).
REASON: Users/scripts pass Source launch options unchanged.
IMPACT: A switch value cannot start with '-' or '+'.

DECISION: `-game` defaults to `hl2`.
REASON: Matches Source launcher behavior for HL2.
IMPACT: None until mounting exists.

DECISION: `-basedir <dir>` (anvil-specific) names the directory holding mod folders; `-game` is relative to it or absolute.
REASON: anvil is not installed next to hl2.exe, so Source's "base dir = exe dir" rule cannot apply.
IMPACT: Default basedir = parent of absolute -game, else current directory.

DECISION: Loose-file lookup tries the exact path, then a per-component case-insensitive scan on case-sensitive hosts.
REASON: Game code requests mixed-case paths; content is mostly lowercase. Windows/macOS filesystems are already insensitive.
IMPACT: Uncached scan per miss on Linux (ponytail note in filesystem.cpp); add a directory cache if profiling shows cost.

DECISION: KeyValues text parser has no escape sequences; conditionals evaluated for host OS ($WIN32/$WINDOWS, $LINUX, $OSX, $POSIX); consoles false.
REASON: Source file loading runs with escapes off; Windows paths contain backslashes.
IMPACT: Escape-enabled consumers (if any) need a flag later.

DECISION: Interface registry is a process-global map; the only global besides logging.
REASON: CreateInterfaceFn is a plain C function pointer handed to game modules; it cannot carry context.
IMPACT: Register before loading game modules; duplicate registration is fatal.

DECISION: Dynamic modules via SDL_LoadObject (already a dependency).
REASON: Handles UTF-8 paths and dlopen/LoadLibrary differences; no need for own wrapper per OS.
IMPACT: RTLD flags not controllable; revisit if game modules need RTLD_GLOBAL symbol sharing (tier0/vstdlib on Linux).

DECISION: Clock default tick 15 ms, frame delta clamped to 0.25 s for simulation (real time unclamped).
REASON: Source default tick interval; clamp avoids catch-up spirals after hitches.
IMPACT: Server DLL tick interval overrides once game modules load.

DECISION: Engine config = `anvil.cfg` in working directory, executed before `+commands`.
REASON: Engine settings are not game data; command line must win.
IMPACT: None.

DECISION: VPK CRC mismatch logs WARN and still returns data.
REASON: Retail HL2 (build 19307283) has one entry (sound/vo/novaprospekt/al_pickherup.wav) whose stored CRC mismatches structurally valid data; Source does not reject such reads.
IMPACT: Real corruption is not blocked at read time; vpk_hl2 test reports mismatches as warnings.

DECISION: VPK part files are opened per read (no handle cache); archive MD5/signature sections ignored.
REASON: Simplest thread-safe design; integrity sections are for distribution tooling, not runtime.
IMPACT: Add handle cache if file-open cost appears in load-time profiles.

DECISION: VTF cubemaps before 7.5 have 7 faces (extra spheremap) unless firstFrame == 0xFFFF; 7.5 has 6.
REASON: Public VTF docs; exact file-size validation passes on all 5237 HL2 textures with this rule.
IMPACT: Renderer uses faces 0-5 only.

DECISION: Materials resolve as dxlevel 95, HDR on, sRGB-capable PC: apply `>=dx90*`, `<shader>_dx9/_dx90`, then `<shader>_hdr_dx9`; ignore `<dx90*` and dx6-dx8 blocks; `srgb?` true, `360?`/`lowfill?` false.
REASON: Matches what a modern PC runs HL2 as; survey of all 5304 HL2 VMTs shows only these block/condition forms.
IMPACT: Options::hdr=false for an LDR path. Unknown conditions log DEBUG and count as false.

DECISION: `patch` shader: insert and replace both overwrite-or-add.
REASON: Simplest behavior that covers map pakfile cubemap patches.
IMPACT: If a game relies on replace skipping absent keys, split the two.

DECISION: Dear ImGui is the internal developer UI (anvil::devui), separate from Source-compatible VGUI. Optional: `-DANVIL_DEVUI=ON` (default OFF) + runtime `-devui`.
REASON: Dev tooling must not shape the VGUI compatibility surface or become a core runtime dependency.
IMPACT: devui/devui.h exposes no ImGui types; OFF build compiles no-op stubs and fetches nothing. ImGui context is a process global private to devui.cpp (accepted exception; not a pattern for other subsystems). FetchContent + pinned URL/SHA256 kept; no vendoring until offline builds are required.

DECISION: One shared low-level 2D path: DevUI -> render::2d -> backend; VGUI -> render::2d -> backend; 3D -> backend. (Confirmed by external review.)
REASON: No per-backend imgui_impl_* code, no renderer logic that exists only for ImGui; VGUI ISurface and devui share primitives.
IMPACT: M3 render::2d minimal API: textured triangles, vertex/index data, alpha blending, scissor/clip rects, texture handles, viewport, batching. No ISurface overbuild in M3. VGUI is never an ImGui wrapper. Until M3 the overlay is built CPU-side but not drawn; devui input needs a platform event hook.

DECISION: First render backend = Vulkan (render/vulkan), one path for Windows, Linux and macOS (MoltenVK as the Apple Vulkan implementation). D3D11 and GLES later; M3 does not wait for them.
REASON: Architect decision (M3). One backend covers every dev/test host.
IMPACT: Instance enables VK_KHR_portability_enumeration (+ ENUMERATE_PORTABILITY flag) when offered; device enables VK_KHR_portability_subset when offered. No macOS-specific renderer.

DECISION: Vulkan is loaded at runtime (no link-time loader): platform finds the library (loader or MoltenVK), SDL3 uses the same file for window surfaces, volk loads entry points from its vkGetInstanceProcAddr. Headers via FetchContent (Vulkan-Headers), volk via FetchContent; GLSL -> SPIR-V at build time with glslangValidator (system, else FetchContent glslang).
REASON: Builds without a Vulkan SDK install; the binary starts (and can fall back to no renderer) on machines without Vulkan.
IMPACT: Apple search list includes Homebrew prefixes for libvulkan/libMoltenVK; SDL_VULKAN_LIBRARY env overrides.

DECISION: Texture pipeline: VTF (formats/vtf) -> anvil texture representation (CPU, format + mips) -> render::Texture handle -> backend resource. VTF code never sees a backend.
REASON: Architect decision; keeps GLES/D3D11 backends possible and parsers testable.
IMPACT: render::Device takes backend-neutral texture descriptions only.

DECISION: BSP pakfile mounts at search-path head with path IDs {GAME, BSP}. Confirmed Source SDK 2013 behavior (external review).
REASON: Map-embedded content must override mod content.
IMPACT: Engine map load: addArchive(pak, ..., front=true); removeArchive on map change.

DECISION: "Entry found in an archive but unreadable -> lookup stops (no fall-through)" is an ANVIL POLICY, not verified Source behavior.
REASON: Avoids silently serving a different copy of a corrupt file. Source's exact fallback semantics are unverified.
IMPACT: Covered by test_vpk (`unreadable entry does not fall through`). Revisit when Source behavior is verified (COMPATIBILITY.md: UNKNOWN).

DECISION: platform caches the loaded Vulkan library for the process (function-local static in platform/window.cpp).
REASON: SDL's surface code and the renderer must resolve entry points from the same library; SDL itself keeps this global.
IMPACT: Another process-lifetime global, confined to platform/. Not a pattern for engine subsystems.

DECISION: Swapchain/offscreen color format is UNORM (B8G8R8A8 or R8G8B8A8), not sRGB.
REASON: Source-era 2D/VGUI colors are gamma-space values blended in gamma space; an sRGB target would double-apply gamma.
IMPACT: World shaders must output gamma-space color (or render to a linear HDR target and tonemap later).

DECISION: Vulkan lifetime model = frame serials + fence-derived completedSerial_ (invariants in render/vulkan/device.cpp header comment). Headless frames are serialized (shared offscreen target). Swapchain recreated only at frame begin after device idle; OUT_OF_DATE/SUBOPTIMAL set a dirty flag; surface format picked by fixed preference (B8G8R8A8_UNORM, R8G8B8A8_UNORM) and a change rebuilds render pass + pipeline. Failed submit = device lost (stop rendering).
REASON: External review sync pass: make GPU completion explicit, fix headless WAR/WAW on the shared target, avoid fence deadlock.
IMPACT: Verified with Khronos validation + synchronization validation on llvmpipe (headless + Xvfb swapchain resize): zero messages. Presentation-engine release of old swapchain images relies on device idle (swapchain_maintenance1 not used).

DECISION: Vulkan memory via VMA v3.4.0 (MIT), pinned by URL + SHA256; implementation in render/vulkan/vma.cpp; entry points from volk (VMA_DYNAMIC_VULKAN_FUNCTIONS=1, STATIC=0).
REASON: Approved in review. Per-resource vkAllocateMemory would hit maxMemoryAllocationCount (often 4096) with world geometry and textures; VMA is the standard, tested sub-allocator.
IMPACT: VMA types stay inside render/vulkan/device.cpp. Host-visible allocations are flushed/invalidated explicitly (non-coherent memory safe).

DECISION: Texture path: materials::textureFromVtf keeps DXT1/3/5 as BC1/2/3 when the device reports textureCompressionBC, else decodes on the CPU; all other VTF formats convert to RGBA8. Frame 0 / face 0 / slice 0 only for now.
REASON: Approved in review; BC optional on desktop Vulkan, required path for GLES later.
IMPACT: RGBA16161616F is a temporary downgrade to RGBA8 (HDR range clamped, WARN per texture) until an HDR format exists; the float path belongs in render::TextureFormat/materials, not in backend code. Conversions for ARGB8888, RGB565, BGRX5551, BGRA5551, BGRA4444 are UNVERIFIED (not used by HL2; D3D channel conventions assumed; test_textures pins the assumed layouts). BC1 = DXT1 on the GPU; CPU decode matches native BC1 in both block modes (test_render, max channel difference 1 on Apple M4). Opaque materials ignore texture alpha, so DXT1 three-color-mode texels render black unless the material uses alpha.

DECISION: One Vulkan device per process.
REASON: volk stores device-level entry points globally (volkLoadDevice).
IMPACT: Tests create devices sequentially. Switch to VolkDeviceTable if multiple devices are ever needed.

DECISION: 3D render API = static meshes + Draw3D (texture * lightmap * colorScale; Opaque / AlphaTest / Translucent). viewProj is column-major, clip space y-up with reverse Z and infinite far plane (depth 1 at near, cleared to 0, GREATER_OR_EQUAL); the Vulkan vertex shader flips y. Depth format D32F > X8D24 > D16 (first supporting depth attachment).
REASON: External review: keep the world representation and LightmappedGeneric logic above the backend. One fixed two-texture modulate covers LightmappedGeneric/UnlitGeneric without backend material knowledge; reverse Z gives float-depth precision over Source's long view distances.
IMPACT: New material models that need more inputs extend Draw3D or add pipelines in the backend; material meaning stays in world/.

DECISION: Lightmaps: style 0, flat sample set, ColorRGBExp32 -> linear (c / 255 * 2^exp) -> gamma 2.2 -> halved into an RGBA8 atlas; LightmappedGeneric draws base * lightmap * 2 in gamma space. LDR lighting lump preferred (HDR lump only when LDR is absent).
REASON: Gamma-space modulate with 2x overbright headroom is the documented look of Source's LDR LightmappedGeneric; one 8-bit atlas, no float textures needed for the first playable map. Independent design, not tuned against Source output pixel-for-pixel.
IMPACT: HDR maps (HDR-only lighting) render without tonemapping; brightness above 2x clamps. Revisit with the HDR/float texture path.

DECISION: Displacement grid: start at the base-face corner nearest dispinfo.startPosition; vertex (row, col) at index row * n + col, rows advance along corner0 -> corner1, columns along corner0 -> corner3. Texture and lightmap coordinates come from the undisplaced (flat) position.
REASON: Orientation determined empirically on all 78 HL2 maps: 67% of non-corner edge vertices coincide with a neighbouring displacement vs 5% for the transposed layout. Flat-position UVs reproduce Source's known texture stretching on displacements.
IMPACT: Test pins the orientation (test_world). Lightmap coordinates of skewed displacement quads are approximate (projection, clamped to the block).

DECISION: World draws model 0 only; surfaces with SKY, SKY2D, NODRAW, HINT, SKIP, TRIGGER are skipped. Water/Refract materials are skipped (STUB), unknown shaders fall back to LightmappedGeneric (PARTIAL, WARN once per shader).
REASON: Brush entities need entity origins/angles (server-side placement) to be positioned; first milestone is the static world.
IMPACT: Doors, func_brush and similar are missing until entities are placed.

DECISION: anvil.cfg and `+commands` execute after window/renderer init.
REASON: `+map` needs the render device; Source likewise runs +commands after engine init.
IMPACT: Config cannot influence window/device creation (none does yet); use command-line switches for that.
