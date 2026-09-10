# Tasks

## M1 leftovers
- [ ] Verify build on Windows (MSVC 2022, vcpkg SDL3).

## M2 (in order)
1. [ ] VPK v1/v2 directory parser (tree, archive index, preload bytes, `_dir` + `_NNN` parts) + synthetic test.
2. [ ] Mount VPK in FileSystem search paths (`foo.vpk` -> `foo_dir.vpk`); lowercase lookup; verify against real HL2 (hl2_pak_dir.vpk).
3. [ ] BSP v19/v20 header + lump table + bounds-checked lump access + test.
4. [ ] BSP lumps for world: planes, vertices, edges, surfedges, faces, texinfo, texdata, models, entities (text).
5. [ ] VTF header + mip/format table (DXT1/3/5, BGRA8888, BGR888, etc.) + test.
6. [ ] VMT = KeyValues + #include/#base resolution + material description.
7. [ ] MDL/VVD/VTX headers + test.
