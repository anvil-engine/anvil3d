# Blockers

None for M1–M3.

## Local test install (dev Mac)
- `~/Library/Application Support/Steam/steamapps/common/Half-Life 2`, buildid 19307283. Use via `-basedir`.
- Assets usable: hl2/*.vpk, hl2/gameinfo.txt, maps, platform/.
- ALL mac binaries are i386 Mach-O (hl2_osx, bin/*.dylib, hl2/bin/client.dylib, server.dylib). macOS 10.15+ cannot load i386. Retail game DLLs are unusable on this machine.

## Open questions (must resolve before M4)
- Game code path: (a) retail client/server binaries — needs Windows/Linux x86/x64 host + replacement tier0/vstdlib with matching exports (MSVC-mangled on Windows) + vtable-exact interfaces; (b) build Source SDK 2013 game code against anvil — check SDK license terms first.
- M4 test host: Linux/Windows machine, or Windows/Linux depot via steamcmd. Dev Mac cannot run retail game code.
