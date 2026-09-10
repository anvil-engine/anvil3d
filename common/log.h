#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define ANVIL_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define ANVIL_PRINTF(fmt, args)
#endif

namespace anvil::log {

enum class Level { Debug, Info, Warning, Error, Fatal };

// Set once at startup, before any worker threads exist.
void setMinLevel(Level level);
void write(Level level, const char* subsystem, const char* fmt, ...) ANVIL_PRINTF(3, 4);
// For programmer errors and unrecoverable engine state. Aborts so a debugger/core dump catches it.
[[noreturn]] void fatal(const char* subsystem, const char* fmt, ...) ANVIL_PRINTF(2, 3);

} // namespace anvil::log

#define ANVIL_DEBUG(sys, ...) ::anvil::log::write(::anvil::log::Level::Debug, sys, __VA_ARGS__)
#define ANVIL_INFO(sys, ...) ::anvil::log::write(::anvil::log::Level::Info, sys, __VA_ARGS__)
#define ANVIL_WARN(sys, ...) ::anvil::log::write(::anvil::log::Level::Warning, sys, __VA_ARGS__)
#define ANVIL_ERROR(sys, ...) ::anvil::log::write(::anvil::log::Level::Error, sys, __VA_ARGS__)
