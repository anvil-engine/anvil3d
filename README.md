# anvil3d

anvil3d is an independent game engine project focused on providing a Source-compatible runtime, with Source SDK 2013 as a primary compatibility reference.

The project aims to implement the engine interfaces, systems, and runtime behavior required to run games from the Source ecosystem without using the original Source engine implementation.

The initial compatibility target is Half-Life 2, with Portal 2 and Team Fortress 2 planned as later targets.

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

0.10.0, early development. Mounts a Source game directory (loose files only, no VPK yet), runs its configs, opens a window. It cannot run any game yet.
See [.agent/COMPATIBILITY.md](.agent/COMPATIBILITY.md).

## Build

See [BUILDING.md](BUILDING.md).

## Run

```
anvil -basedir "<Half-Life 2 install dir>" -game hl2 [-w 1280 -h 720] [-full | -windowed] [-dev] [-devui] [-norender] [-novsync] [-vkdebug] [+exec <cfg>]
```

`-basedir` is the folder that contains `hl2/` (e.g. `steamapps/common/Half-Life 2`). Game data must come from your own legally obtained installation; anvil ships none.
Engine settings can go in `anvil.cfg` in the working directory (console commands, one per line).

## License

MIT. See [LICENSE](LICENSE).
