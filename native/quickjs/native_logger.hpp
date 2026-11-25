// Native logger: durable diagnostic trace for Readex QuickJS module.
// Primary: /userdisk/readex/app.native.log; fallbacks: /userdisk/readex-native.log,
// /tmp/readex-native.log, then stderr. Never silently swallow logger failures.
// SPDX-License-Identifier: GPL-3.0
#ifndef READEX_NATIVE_LOGGER_HPP
#define READEX_NATIVE_LOGGER_HPP

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace readex_native_log {

inline bool write_to(const char* path, const char* line, size_t length) noexcept
{
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return false;
    size_t written = 0;
    while (written < length) {
        const ssize_t n = ::write(fd, line + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ::close(fd); return false; }
        written += static_cast<size_t>(n);
    }
    const bool ok = (::fsync(fd) == 0);
    ::close(fd);
    return ok;
}

inline void write(const char* level, const char* stage, const char* detail = nullptr) noexcept
{
    (void)::mkdir("/userdisk/readex", 0755);
    std::time_t now = std::time(nullptr);
    std::tm tm_now;
    char timestamp[32] = "unknown-time";
    if (::localtime_r(&now, &tm_now) != nullptr)
        (void)::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);

    char line[2048];
    const int n = std::snprintf(line, sizeof(line), "[%s] [%s] %s%s%s\n",
        timestamp, level ? level : "INFO", stage ? stage : "native",
        detail && detail[0] ? " " : "", detail && detail[0] ? detail : "");
    if (n <= 0) return;
    const size_t length = static_cast<size_t>(n) < sizeof(line) ?
        static_cast<size_t>(n) : sizeof(line) - 1;

    if (write_to("/userdisk/readex/app.native.log", line, length)) return;
    if (write_to("/userdisk/readex-native.log", line, length)) return;
    if (write_to("/tmp/readex-native.log", line, length)) return;
    (void)::write(STDERR_FILENO, line, length);
}

} // namespace readex_native_log
#endif
