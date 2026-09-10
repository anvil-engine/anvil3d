# Status

Version: 0.10.0 (dev)
Milestone: M1 Runtime Core — DONE except Windows build check (2026-09-10). Next: M2 Asset Foundation.

## Working
- Build verified: macOS arm64 (AppleClang 21), Linux arm64 (Ubuntu 25.04, GCC 14, via podman). Windows unverified.
- common/: logging, Source cmdline, KeyValues text parser (conditionals, depth limit).
- filesystem/: search paths with path IDs, path normalization + traversal rejection, case-insensitive lookup on Linux, gameinfo.txt parsing + mounting (dirs, `dir/*`).
- platform/: SDL3 window, dynamic module loading (SDL_LoadObject).
- compat/: engine interface registry + CreateInterface factory ABI.
- engine/: console (cvars, commands, exec, quit, echo, cvarlist), Clock (fixed tick, pause, timescale), fps_max loop.
- Real HL2 install: gameinfo.txt parses, 5 dirs mount, loose cfg/skill.cfg executes.
- Tests: cmdline, keyvalues, filesystem, console(+clock), interfaces (real module load), smoke (tests/data/testgame).

## Stubbed / not implemented
- VPK search paths: logged WARN and skipped (M2).
- gameinfo implicit rules (auto gamebin, _<language> dirs, low-violence game_lv) not applied.
- cvar flags (cheat/archive), config.cfg saving, autocomplete, in-game console UI.
- Simulation ticks computed but nothing consumes them.

## Known broken
- none
