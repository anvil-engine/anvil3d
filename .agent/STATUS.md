# Status

Version: 0.10.0 (dev)
Milestone: M2 Asset Foundation — in progress. VPK, BSP world lumps, VTF done; next VMT.

## Working
- Build verified: macOS arm64 (AppleClang 21), Linux arm64 (Ubuntu 25.04, GCC 14, via podman). Windows unverified.
- common/: logging, Source cmdline, KeyValues text parser (conditionals, depth limit).
- filesystem/: search paths with path IDs, path normalization + traversal rejection, case-insensitive lookup on Linux, gameinfo.txt parsing + mounting (dirs, `dir/*`).
- platform/: SDL3 window, dynamic module loading (SDL_LoadObject).
- compat/: engine interface registry + CreateInterface factory ABI.
- engine/: console (cvars, commands, exec, quit, echo, cvarlist), Clock (fixed tick, pause, timescale), fps_max loop.
- filesystem/vpk: VPK v1/v2 tree, parts + embedded data, preload, CRC (warn-only), bounds-checked; mounted from gameinfo.
- Real HL2 install: 6 VPKs + 5 dirs mount; all 36,015 VPK entries (3.5 GB) read OK (`-DANVIL_HL2_DIR=...` enables test vpk_hl2).
- formats/bsp: VBSP v19/v20 header, lumps (entities, planes, verts, edges, surfedges, texinfo, texdata+names, faces LDR/HDR, models, lighting); all cross-refs validated at load. 78/78 non-empty HL2 maps load (`bsp_hl2` test).
- formats/vtf: VTF 7.0-7.5 header, resources, cubemap faces, per mip/frame/face/slice image lookup, exact size validation. 5237/5237 HL2 textures parse (`vtf_hl2`). No pixel decoding yet.
- Tests: cmdline, keyvalues, filesystem, console(+clock), interfaces (real module load), smoke (tests/data/testgame).

## Stubbed / not implemented
- gameinfo implicit rules (auto gamebin, _<language> dirs, low-violence game_lv) not applied.
- cvar flags (cheat/archive), config.cfg saving, autocomplete, in-game console UI.
- Simulation ticks computed but nothing consumes them.

## Known broken
- none
