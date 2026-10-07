#pragma once
#include <cstdio>
#include <cstdarg>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace gtabr {
inline void logf(int level, const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
#ifdef __ANDROID__
  __android_log_print(level == 2 ? ANDROID_LOG_ERROR : (level == 1 ? ANDROID_LOG_WARN : ANDROID_LOG_INFO), "GTABR", "%s", buf);
#else
  fprintf(level == 2 ? stderr : stdout, "[%s] %s\n", level == 2 ? "ERR" : (level == 1 ? "WRN" : "INF"), buf);
  fflush(stdout);
#endif
}
}  // namespace gtabr
#define LOGI(...) ::gtabr::logf(0, __VA_ARGS__)
#define LOGW(...) ::gtabr::logf(1, __VA_ARGS__)
#define LOGE(...) ::gtabr::logf(2, __VA_ARGS__)
