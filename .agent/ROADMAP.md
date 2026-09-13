# Roadmap

- M0 Bootstrap — CMake, platform, logging, cmdline, window. DONE
- M1 Runtime Core — VFS + search paths, gameinfo.txt (KeyValues), module loading, interface registry, console, cvars, timing. DONE (Windows unverified)
- M2 Asset Foundation — VPK, BSP, VTF, VMT, MDL/VVD/VTX. DONE
- M3 Renderer — render API + first backend, textures, shaders, buffers, BSP world. IN PROGRESS: world, PVS/frustum, 2D sky, brush entities, WVT, static props done; remaining: prop lighting, translucent sorting (needs per-draw depth/order data), water, 3D skybox, decals/overlays
- Priority: Fix map lighting from original Source data: correct world, static-prop, and entity lighting; continue the main roadmap.
- M4 HL2 Boot — game detection, mounting, game DLL load, required interfaces, map load, world render
- M5 Playable HL2 — player, input, physics (Jolt), entities, sound, VGUI, save/load
- M6 Expansion — Portal 2, TF2, networking

## Month target

Deliver one honest, playable HL2 vertical slice from an original map spawn to its authored transition, using generic map entities and original assets. Priorities: generic movers and I/O; audio and map lighting; the minimum scripted/NPC behavior and `changelevel`; then deterministic end-to-end replay and an unsupported-feature audit. This target does not promise the full campaign or retail game DLL compatibility.

0.10.0 done = M5 minimal: first map loads, renders, player moves, basic physics/audio/VGUI.
