# Decisions

DECISION: MIT license.
REASON: Permissive, per project brief.
IMPACT: Dependencies must be MIT/BSD/zlib/Apache-compatible.

DECISION: CMake >= 3.20, C++20, Ninja recommended.
REASON: Portable; C++20 available on MSVC 2022, GCC 11+, Clang 14+.
IMPACT: No compiler extensions (CMAKE_CXX_EXTENSIONS OFF).

DECISION: SDL3 for window/input/events, found via find_package (not vendored).
REASON: Mature, zlib license, covers Windows/Linux/Android, Vulkan/GL/GLES surface creation.
IMPACT: Users install SDL3 dev package or pass CMAKE_PREFIX_PATH. Linked PRIVATE to anvil_platform.

DECISION: Jolt Physics for M5 (not integrated yet).
REASON: MIT, deterministic option, character controller, constraints, raycasts, used in shipped games.
IMPACT: Wrapped behind anvil physics interfaces; game never sees Jolt types.

DECISION: Command line follows Source semantics (case-insensitive `-parm value`, `+cmd args`).
REASON: Users/scripts pass Source launch options unchanged.
IMPACT: A switch value cannot start with '-' or '+'.

DECISION: `-game` defaults to `hl2`.
REASON: Matches Source launcher behavior for HL2.
IMPACT: None until mounting exists.

DECISION: `-basedir <dir>` (anvil-specific) names the directory holding mod folders; `-game` is relative to it or absolute.
REASON: anvil is not installed next to hl2.exe, so Source's "base dir = exe dir" rule cannot apply.
IMPACT: Default basedir = parent of absolute -game, else current directory.

DECISION: Loose-file lookup tries the exact path, then a per-component case-insensitive scan on case-sensitive hosts.
REASON: Game code requests mixed-case paths; content is mostly lowercase. Windows/macOS filesystems are already insensitive.
IMPACT: Uncached scan per miss on Linux (ponytail note in filesystem.cpp); add a directory cache if profiling shows cost.

DECISION: KeyValues text parser has no escape sequences; conditionals evaluated for host OS ($WIN32/$WINDOWS, $LINUX, $OSX, $POSIX); consoles false.
REASON: Source file loading runs with escapes off; Windows paths contain backslashes.
IMPACT: Escape-enabled consumers (if any) need a flag later.

DECISION: Interface registry is a process-global map; the only global besides logging.
REASON: CreateInterfaceFn is a plain C function pointer handed to game modules; it cannot carry context.
IMPACT: Register before loading game modules; duplicate registration is fatal.

DECISION: Dynamic modules via SDL_LoadObject (already a dependency).
REASON: Handles UTF-8 paths and dlopen/LoadLibrary differences; no need for own wrapper per OS.
IMPACT: RTLD flags not controllable; revisit if game modules need RTLD_GLOBAL symbol sharing (tier0/vstdlib on Linux).

DECISION: Clock default tick 15 ms, frame delta clamped to 0.25 s for simulation (real time unclamped).
REASON: Source default tick interval; clamp avoids catch-up spirals after hitches.
IMPACT: Server DLL tick interval overrides once game modules load.

DECISION: Engine config = `anvil.cfg` in working directory, executed before `+commands`.
REASON: Engine settings are not game data; command line must win.
IMPACT: None.
