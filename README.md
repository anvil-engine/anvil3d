# anvil3d

Open-source, clean-room runtime for games built on the Source Engine SDK 2013 ecosystem. First target: Half-Life 2.

Not affiliated with or endorsed by Valve. Contains no Valve code or game content.

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
