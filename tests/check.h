#pragma once

#include <cstdio>

// Minimal test harness: works in Release (unlike assert), counts failures, test main returns TEST_RESULT().
inline int g_failures = 0;

#define CHECK(x)                                                                    \
  do {                                                                              \
    if (!(x)) {                                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);   \
      ++g_failures;                                                                 \
    }                                                                               \
  } while (0)

#define TEST_RESULT() (g_failures == 0 ? (std::puts("ok"), 0) : 1)
