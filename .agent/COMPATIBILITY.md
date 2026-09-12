# Compatibility

Statuses: NOT STARTED, STUB, PARTIAL, WORKING, COMPATIBLE, UNKNOWN. Claim nothing untested.

| System      | HL2         | Portal 2    | TF2         |
|-------------|-------------|-------------|-------------|
| Cmdline     | PARTIAL     | PARTIAL     | PARTIAL     |
| KeyValues   | PARTIAL     | UNKNOWN     | UNKNOWN     |
| Gameinfo    | PARTIAL     | UNKNOWN     | UNKNOWN     |
| Filesystem  | PARTIAL     | NOT STARTED | NOT STARTED |
| Console     | PARTIAL     | NOT STARTED | NOT STARTED |
| VPK         | WORKING     | UNKNOWN     | UNKNOWN     |
| BSP         | PARTIAL (parse incl. brushes; no areaportals/water/overlays/cubemaps) | NOT STARTED | NOT STARTED |
| Materials   | PARTIAL     | NOT STARTED | NOT STARTED |
| Textures    | PARTIAL (VTF->GPU; no frames/cubemaps/HDR) | NOT STARTED | NOT STARTED |
| World render | PARTIAL (brush world, LightmappedGeneric-style, lightmaps, displacements, PVS/frustum, 2D sky, static brush entities, WVT blend, static props with ambient-only light; no 3D sky/water/decals/overlays) | NOT STARTED | NOT STARTED |
| Models      | PARTIAL (LOD0 mesh, bind-pose skeleton and local sequence metadata; no animated pose decoding) | NOT STARTED | NOT STARTED |
| Physics     | PARTIAL (Jolt walking, static BSP collision; PHY props unsupported; triangle approximation diagnostic only) | NOT STARTED | NOT STARTED |
| VGUI        | PARTIAL (resources/localization, schemes, common panel runtime, font/text batching and limited authored-command dispatch) | NOT STARTED | NOT STARTED |
| Weapon scripts | PARTIAL (manifest, core WeaponData paths/ammo/SoundData fields) | NOT STARTED | NOT STARTED |
| Client DLL  | NOT STARTED | NOT STARTED | NOT STARTED |
| Server DLL  | NOT STARTED | NOT STARTED | NOT STARTED |
| Audio       | NOT STARTED | NOT STARTED | NOT STARTED |
| Networking  | NOT STARTED | NOT STARTED | NOT STARTED |

## Behavior notes
- Archive entry found but unreadable -> lookup stops: anvil policy, Source behavior UNKNOWN.
- BSP pakfile at search-path head, IDs GAME+BSP: matches Source SDK 2013 (external review).
- Lightmap shading (gamma 2.2 encode, base * lightmap * 2): anvil approximation of LDR LightmappedGeneric, not pixel-verified.
- RGBA16161616F textures: PARTIAL, downgraded to RGBA8 (WARN at load).
- VTF ARGB8888/RGB565/BGRX5551/BGRA5551/BGRA4444: UNVERIFIED channel layouts (not in HL2).
- Displacement grid orientation: derived from HL2 data (edge coincidence), not verified against Source output.
- 2D skybox: face layout derived from HL2 data (texture seams, sun direction); not compared with Source screenshots. $basetexturetransform rotation ignored (PARTIAL, WARN). 3D skybox (sky_camera): NOT STARTED.
- WorldVertexTransition: linear vertex-alpha blend, direction checked against HL2 terrain data; $blendmodulatetexture / $basetexturetransform2 ignored (PARTIAL).
- Static props: placement and materials PARTIAL; lighting is a per-prop leaf-ambient approximation (no direct light, no VHV), not verified against Source output; fade = hard cull at fademaxdist.
- Brush entity placement: origin-relative models verified on HL2 data; angle rotation (esp. roll sign) UNVERIFIED; no entity render modes.
- Shader behavior (LightmappedGeneric, UnlitGeneric, sky, fallbacks for other shaders): anvil interpretation, not verified against Source output.

- Independent Jolt player: 32x32x72 rounded box, 64-unit eye, 600 units/s² gravity, 190/320 walk/run, 265 jump impulse, 18-unit stairs. Gameplay tuning, not SDK movement/prediction compatibility. No crouch, water, ladders, moving platforms or entity simulation yet.
- Collision brushside dispinfo is unused metadata: all 22741 brushsides in trainstation_01 store zero, including non-displacement sides; maps without a displacement lump also store zero. Face.dispinfo remains the validated displacement reference. Static prop render triangles approximate collision and can differ from Source PHY hulls.

- Independent menu/HUD/combat/targets are restricted to explicit `-diagnosticplay`; they are not Source compatibility and are absent from normal map inspection. Authored GameMenu entries are inspected through `vgui_menu`, not substituted with the diagnostic UI.
- VGUI resource lookup uses all authored search paths in order, including PLATFORM (required by the installed SourceScheme.res -> SourceSchemeBase.res reference). Relative #base inheritance fills missing keys; #include appends root peers. Broader branch behavior remains unverified.
- Scheme interpretation preserves fallback order and all authored font attributes; inclusive yres and Unicode range filtering accepts numbered and direct face definitions observed in installed content. RGB[A] and Colors/BaseSettings aliases resolve with explicit errors for missing/invalid/cyclic values.
- CustomFontFiles are read from mounted content and registered in authored order with FreeType. Bounded platform font discovery adds only exact authored family/style matches, without nearest-family substitution. Actual Unicode glyph availability determines the first usable face; grayscale/mono coverage and bearings/advance are produced. Bitmap VBF fonts, weight synthesis, blur/outline/shadow/additive effects, kerning/shaping, proportional scaling and atlases remain unsupported.
- Single-line UTF-8 text can be composed from those glyphs into one RGBA8 coverage texture and `render::Batch2D` quad. Input is strict and capped at 4096 scalars, 16384-pixel bounds and 64 MiB coverage. This is a render primitive, not a recreated game panel; multiline layout, accelerator handling and persistent atlases remain unsupported.
- Panel resources preserve authored control type/name, localized label/title, command, visibility/enabled/default state, tab order and geometry. Integer coordinates plus observed center (`cN`), far-edge (`rN`) and fill (`fN`) forms resolve within bounded parent dimensions.
- Basic Panel/EditablePanel/Frame, Label, Button and Divider instances preserve resolved bounds and runtime state. Button focus honors authored Default/tab positions, skips hidden/disabled controls, wraps and hit-tests topmost-first. The diagnostic executes Close, ResumeGame, Quit and `engine ...`; other commands are reported unsupported. Unknown control classes remain explicit Unsupported instances.
- Scheme Borders aliases and side layers resolve with authored order, color and offset. Common controls produce a backend-neutral plan of scheme-derived fills, borders and text requests. Fills/borders and exact-family text textures submit through `render::2d`; each text run honors authored alignment and clips to its control. Unknown controls remain visibly unsupported.
- KeyValues escaping is opt-in for localization (quotes/backslashes/newline/return/tab); other consumers preserve literal backslashes. Empty blocks are distinguished from empty strings for base inheritance.

- Normal collision loading omits unsupported static-prop PHY shapes with a warning. Render-triangle substitutes are enabled only on the explicit diagnostic path.
