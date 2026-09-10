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
| BSP         | PARTIAL (parse; no brushes/areaportals/water/overlays/cubemaps) | NOT STARTED | NOT STARTED |
| Materials   | PARTIAL     | NOT STARTED | NOT STARTED |
| Textures    | PARTIAL (VTF->GPU; no frames/cubemaps/HDR) | NOT STARTED | NOT STARTED |
| World render | PARTIAL (brush world, LightmappedGeneric-style, lightmaps, displacements; no sky/water/WVT blend/brush entities/props) | NOT STARTED | NOT STARTED |
| Models      | PARTIAL     | NOT STARTED | NOT STARTED |
| Physics     | NOT STARTED | NOT STARTED | NOT STARTED |
| VGUI        | NOT STARTED | NOT STARTED | NOT STARTED |
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
