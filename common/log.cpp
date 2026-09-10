#include "common/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace anvil::log {
namespace {

std::mutex g_mutex;
Level g_minLevel = Level::Info;

const char* levelName(Level level) {
  switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warning: return "WARN";
    case Level::Error: return "ERROR";
    case Level::Fatal: return "FATAL";
  }
  return "?";
}

void vwrite(Level level, const char* subsystem, const char* fmt, va_list args) {
  if (level < g_minLevel) return;
  std::lock_guard lock(g_mutex);
  std::fprintf(stderr, "[%s] %s: ", levelName(level), subsystem);
  std::vfprintf(stderr, fmt, args);
  std::fputc('\n', stderr);
}

} // namespace

void setMinLevel(Level level) { g_minLevel = level; }

void write(Level level, const char* subsystem, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vwrite(level, subsystem, fmt, args);
  va_end(args);
}

void fatal(const char* subsystem, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vwrite(Level::Fatal, subsystem, fmt, args);
  va_end(args);
  std::fflush(stderr);
  std::abort();
}

} // namespace anvil::log
