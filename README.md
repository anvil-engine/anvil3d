# anvil3d

anvil3d is an independent game engine project focused on providing a Source-compatible runtime, with Source SDK 2013 as a primary compatibility reference.

The project aims to implement the engine interfaces, systems, and runtime behavior required to run games from the Source ecosystem without using the original Source engine implementation.

The initial compatibility target is Half-Life 2. Later targets include its Episodes, Team Fortress 2, Portal, Portal 2, Counter-Strike: Source, and other Source games. Each target requires separate compatibility validation.

The project is developed independently and does not contain Source engine code or game assets.

The project is not affiliated with or endorsed by Valve Corporation.

## Goals

* Source-compatible runtime
* Clean-room independent implementation
* Half-Life 2 support as the initial target
* Cross-platform development
* Vulkan, Direct3D 11, and OpenGL ES rendering backends
* Open-source engine and tooling
* Compatibility with Source-style game data, interfaces, and content

## Status

0.10.0, early development (M3 renderer in progress). Mounts loose files and VPK archives, runs configs, and renders HL2 BSP maps through Vulkan with textures, lightmaps, displacements, PVS/frustum culling, a 2D skybox, static brush entities, and ambient-lit static props. An independent Jolt player supports WASD walking, collision, gravity, jumping, and stairs; a free camera remains available through noclip. Original VGUI resource trees/localization can be inspected, but game UI execution, game DLL integration and gameplay entities are not implemented. It cannot run the HL2 game yet.
See [.agent/COMPATIBILITY.md](.agent/COMPATIBILITY.md).

## Build

See [BUILDING.md](BUILDING.md).

## Run

```
anvil -basedir "<Half-Life 2 install dir>" -game hl2 [-w 1280 -h 720] [-full | -windowed] [-dev] [-devui] [-norender] [-novsync] [-vkdebug] [-noclip] [+exec <cfg>] [+map d1_trainstation_01]
```

`-basedir` is the folder that contains `hl2/` (e.g. `steamapps/common/Half-Life 2`). Game data must come from your own legally obtained installation; anvil ships none.
Controls: WASD move, mouse look, Space jump, Shift run, Escape release/capture the mouse. `-noclip` starts the developer free camera (RMB look, Space/Ctrl vertical movement); the `noclip` console command toggles modes.

Engine settings can go in `anvil.cfg` in the working directory (console commands, one per line).

## License

MIT. See [LICENSE](LICENSE).

## Compatibility direction

Original content and runtime semantics drive the implementation. Anvil does not replace game UI, HUDs, animations, sounds or map progression with custom recreations. Its renderer may use modern techniques while preserving asset semantics.

`+vgui_menu` prints original GameMenu.res entries with game localization. `+vgui_resource resource/sourcescheme.res` loads the authored tree, resolving base/include resources through the mounted filesystem. These commands diagnose resources; they do not execute the game UI.

`+vgui_scheme resource/clientscheme.res ClientTitleFont 768` inspects the original font variants for a 768-pixel screen height and character `A`, loads declared custom font files through VFS, and rasterizes the glyph when an original face supplies it. VGUI schemes resolve named colors and BaseSettings aliases while preserving font fallback order and resolution/character ranges. Original panel descriptors now instantiate basic controls with authored text, commands, state, focus order and relative geometry. Single-line UTF-8 runs can reach the 2D renderer; proportional scaling, kerning/shaping, multiline layout, reusable atlases, panel painting and command dispatch remain unsupported.
`+vgui_panel resource/newgamedialog.res 600 296` instantiates supported original controls for diagnostics, resolves their local bounds and reports game-specific controls that still need implementations. It draws original SourceScheme fills, layered borders and localized text through `render::2d`; custom fonts come from mounted content and platform fonts require an exact authored family/style match. Buttons follow original visibility, enabled state and tab order and yield authored commands; game-specific controls and command dispatch remain unsupported.

The earlier independent combat/menu test rig is available only with `-diagnosticplay`. It creates practice targets and uses its own test weapons/UI; it is explicitly **not Source game compatibility**. Without that flag, start map inspection with `+map <name>`.
