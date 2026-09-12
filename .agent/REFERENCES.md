# References

## Dependencies
| Name | Version | License | How | Used by |
|------|---------|---------|-----|---------|
| SDL3 | >= 3.2 (dev: 3.4.16) | zlib | system package, `find_package(SDL3 CONFIG)` | platform/ only |
| Dear ImGui | v1.92.9b | MIT | FetchContent, pinned URL + SHA256, only with `-DANVIL_DEVUI=ON` | devui/ only |
| Vulkan-Headers | vulkan-sdk-1.4.357.0 | Apache-2.0 OR MIT | FetchContent pinned (ANVIL_VULKAN) | render/vulkan |
| volk | vulkan-sdk-1.4.357.0 | MIT | FetchContent pinned, volk.c compiled in | render/vulkan |
| VulkanMemoryAllocator | v3.4.0 | MIT | FetchContent pinned (ANVIL_VULKAN), header + vma.cpp | render/vulkan |
| glslang | vulkan-sdk-1.4.357.0 | BSD-3-Clause + others (see its LICENSE.txt) | system glslangValidator, else FetchContent (build tool only, not shipped) | shader build |
| Vulkan runtime | system | loader Apache-2.0; MoltenVK Apache-2.0 | dlopen at runtime (never linked) | render/vulkan |
| Jolt Physics | v5.6.0 | MIT | FetchContent URL + SHA256 | physics/ |
| FreeType | 2.14.3 | FreeType License | FetchContent URL + SHA256 | vgui/ custom font loading/rasterization |

- Nothing is vendored, so `licenses/` does not exist yet. Binary distributions must ship license texts for SDL3 (zlib), volk (MIT), VMA (MIT), Vulkan-Headers (Apache-2.0/MIT), MoltenVK if bundled (Apache-2.0), FreeType (FTL), and, with devui, ImGui (MIT); add `licenses/` with packaging.
- ImGui upstream: https://github.com/ocornut/imgui (tarball `archive/refs/tags/v1.92.9b.tar.gz`, sha256 21d8a0a5...7f99).

## Format / behavior sources (clean-room: public docs + observation of user-owned game files)
- VGUI scheme interpretation: observed Colors/BaseSettings, Fonts (including direct face definitions), yres/range and CustomFontFiles in the installed ClientScheme.res and PLATFORM SourceSchemeBase.res. The authored comments specify ordered font fallbacks; no Valve implementation was inspected or copied for this subsystem.
- FreeType public API/reference for memory-backed faces, pixel sizing, Unicode glyph lookup and rendering: https://freetype.org/freetype2/docs/tutorial/step1.html. Pinned upstream release: https://gitlab.freedesktop.org/freetype/freetype/-/tags/VER-2-14-3.
- VPK, BSP, VTF, VMT, MDL/VVD/VTX, gameinfo.txt, KeyValues: Valve Developer Community wiki format pages; layouts verified against a retail HL2 install (see BLOCKERS.md for build id).
- Source interface factory contract (CreateInterface name/return code): public Source SDK headers' documented behavior; no SDK code copied.

- Jolt API/build reference: https://github.com/jrouwe/JoltPhysics/releases/tag/v5.6.0 and fetched Jolt public headers. Binary distributions must include its MIT license.
- BSP collision record layout only: https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/bspfile.h (public structs for brushes, brushsides, leafbrushes). No engine implementation copied; hull clipping is an independent geometric construction.
