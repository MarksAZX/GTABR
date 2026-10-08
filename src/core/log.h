#pragma once
#include <cstdio>
#include <cstdarg>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace gtabr {
// Ring of the most recent log lines: written to crash.txt by the crash handler (core/crashlog.h) so a crash on a phone can be read
// back on the next start without adb.
struct LogRing {
  static constexpr int kLines = 48, kLen = 200;
  char lines[kLines][kLen] = {};
  int next = 0;
};
inline LogRing& logRing() { static LogRing r; return r; }
inline void logf(int level, const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  {
    LogRing& r = logRing();
    snprintf(r.lines[r.next], LogRing::kLen, "%s %s", level == 2 ? "E" : (level == 1 ? "W" : "I"), buf);
    r.next = (r.next + 1) % LogRing::kLines;
  }
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
