# References

## Dependencies
| Name | Version | License | How | Used by |
|------|---------|---------|-----|---------|
| SDL3 | >= 3.2 (dev: 3.4.16) | zlib | system package, `find_package(SDL3 CONFIG)` | platform/ only |
| Dear ImGui | v1.92.9b | MIT | FetchContent, pinned URL + SHA256, only with `-DANVIL_DEVUI=ON` | devui/ only |
| Jolt Physics | not integrated | MIT | planned (M5) | physics/ |

- Nothing is vendored, so `licenses/` does not exist yet. Binary distributions must ship SDL3 (zlib) and, if built with devui, ImGui (MIT) license texts; add `licenses/` with packaging.
- ImGui upstream: https://github.com/ocornut/imgui (tarball `archive/refs/tags/v1.92.9b.tar.gz`, sha256 21d8a0a5...7f99).

## Format / behavior sources (clean-room: public docs + observation of user-owned game files)
- VPK, BSP, VTF, VMT, MDL/VVD/VTX, gameinfo.txt, KeyValues: Valve Developer Community wiki format pages; layouts verified against a retail HL2 install (see BLOCKERS.md for build id).
- Source interface factory contract (CreateInterface name/return code): public Source SDK headers' documented behavior; no SDK code copied.
