#pragma once

// Source interface factory ABI, as seen by game modules (from the public SDK's interface.h contract):
//   extern "C" void* CreateInterface(const char* name, int* returnCode);
// - name: interface name + 3-digit version, e.g. "VEngineClient015". Exact, case-sensitive match.
// - returnCode: may be null; set to IFACE_OK / IFACE_FAILED.
// - Returned pointer is a C++ object with a vtable laid out for the target compiler
//   (MSVC on Windows, GCC Itanium ABI on Linux/macOS). Owned by the providing module; never freed by the caller.
// - Calling convention: platform default C (cdecl on x86 Windows).

namespace anvil::platform { class Module; }

namespace anvil {

using CreateInterfaceFn = void* (*)(const char* name, int* returnCode);
enum InterfaceReturn { IFACE_OK = 0, IFACE_FAILED = 1 };

#if defined(_WIN32)
#define ANVIL_EXPORT extern "C" __declspec(dllexport)
#else
#define ANVIL_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Engine-side registry backing engineFactory. Register at startup, before any game module loads.
// Global by necessity: CreateInterfaceFn is a plain function pointer and cannot carry context.
void registerInterface(const char* name, void* iface);
void* engineFactory(const char* name, int* returnCode);

// The module's exported "CreateInterface", or null (logged) if it has none.
CreateInterfaceFn moduleFactory(const platform::Module& module);

} // namespace anvil
