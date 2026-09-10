# Agent rules

Read first: STATUS.md, BLOCKERS.md, TASKS.md. Then only files relevant to the task.

## Hard rules
- Clean-room. Never copy/port/decompile Valve Source Engine code. Public SDK headers, docs, observed formats only. Unsure -> independent design.
- Never commit game assets, Valve binaries, build dirs, local absolute paths.
- Game data = untrusted input. Bounds-check every binary read.
- HL2 compatibility beats new features and pretty architecture.
- Stubs must be labeled (STUB/PARTIAL in COMPATIBILITY.md, WARN log at runtime). Never fake silently.

## Workflow
1. Pick next item from TASKS.md. Smallest vertical slice.
2. Implement, build, test (commands below).
3. Update STATUS.md / TASKS.md / DECISIONS.md / COMPATIBILITY.md only where changed.
4. Report: `DONE: / NEXT: / BLOCKED:`. Build failure: `ERROR: / CAUSE: / FIX:`. No essays.

## Build / test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Code
- C++20, RAII, explicit ownership, no singletons/global state except logging and the interface registry.
- SDL only inside platform/. Backend APIs only inside renderer/<backend>/.
- Log with ANVIL_DEBUG/INFO/WARN/ERROR(subsystem, fmt, ...) from common/log.h.
- Comments: why, compat assumptions, ABI, ownership. Not what.
- Every binary format parser gets a test in tests/ using synthetic data.
- `ponytail:` comment marks a deliberate shortcut + upgrade path.
