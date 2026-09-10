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
| Jolt Physics | not integrated | MIT | planned (M5) | physics/ |

- Nothing is vendored, so `licenses/` does not exist yet. Binary distributions must ship license texts for SDL3 (zlib), volk (MIT), VMA (MIT), Vulkan-Headers (Apache-2.0/MIT), MoltenVK if bundled (Apache-2.0) and, with devui, ImGui (MIT); add `licenses/` with packaging.
- ImGui upstream: https://github.com/ocornut/imgui (tarball `archive/refs/tags/v1.92.9b.tar.gz`, sha256 21d8a0a5...7f99).

## Format / behavior sources (clean-room: public docs + observation of user-owned game files)
- VPK, BSP, VTF, VMT, MDL/VVD/VTX, gameinfo.txt, KeyValues: Valve Developer Community wiki format pages; layouts verified against a retail HL2 install (see BLOCKERS.md for build id).
- Source interface factory contract (CreateInterface name/return code): public Source SDK headers' documented behavior; no SDK code copied.
