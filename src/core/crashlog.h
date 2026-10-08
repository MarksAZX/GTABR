// Crash capture: on SIGSEGV / SIGABRT / SIGBUS / SIGFPE / SIGILL the last log lines are written to a file, then the default action
// runs. The game shows the file on the next start (what the phone was doing right before it closed).
#pragma once
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "log.h"

namespace gtabr {
namespace crashlog {
inline char gPath[512] = {};
inline void writeAll(int fd, const char* s) { size_t n = strlen(s); while (n > 0) { ssize_t w = ::write(fd, s, n); if (w <= 0) break; s += w; n -= (size_t)w; } }
inline void onSignal(int sig) {
  int fd = ::open(gPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    const char* name = sig == SIGSEGV ? "SIGSEGV (acesso invalido a memoria)" : sig == SIGABRT ? "SIGABRT (abortado)" : sig == SIGBUS ? "SIGBUS" : sig == SIGFPE ? "SIGFPE" : "SIGILL";
    writeAll(fd, "sinal: "); writeAll(fd, name); writeAll(fd, "\n");
    LogRing& r = logRing();
    for (int i = 0; i < LogRing::kLines; ++i) {
      const char* l = r.lines[(r.next + i) % LogRing::kLines];
      if (!l[0]) continue;
      writeAll(fd, l); writeAll(fd, "\n");
    }
    ::close(fd);
  }
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
inline void install(const std::string& path) {
  strncpy(gPath, path.c_str(), sizeof(gPath) - 1);
  for (int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) std::signal(s, onSignal);
}
}  // namespace crashlog
}  // namespace gtabr
