// native_logger.hpp — Native Core / QuickJS binding diagnostics
// Writes only to /userdisk/readex/app.native.log. UI logging is handled by
// src/utils/Logger.js and must never share this file.
//
// SPDX-License-Identifier: GPL-3.0
#ifndef READEX_NATIVE_LOGGER_HPP
#define READEX_NATIVE_LOGGER_HPP

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace readex_native_log {

inline void write(const char* level, const char* stage, const char* detail = nullptr) noexcept
{
    const char* path = "/userdisk/readex/app.native.log";
    // Do not depend on the UI logger having initialized the directory first.
    (void)::mkdir("/userdisk/readex", 0755);

    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;

    std::time_t now = std::time(nullptr);
    std::tm tm_now;
    char timestamp[32] = "unknown-time";
    if (::localtime_r(&now, &tm_now) != nullptr)
        (void)std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);

    char line[2048];
    const int length = std::snprintf(
        line, sizeof(line), "[%s] [%s] %s%s%s\n",
        timestamp,
        level ? level : "INFO",
        stage ? stage : "native",
        detail && detail[0] ? " " : "",
        detail && detail[0] ? detail : "");

    if (length > 0) {
        const size_t count = static_cast<size_t>(length) < sizeof(line)
            ? static_cast<size_t>(length) : sizeof(line) - 1;
        (void)::write(fd, line, count);
    }
    (void)::close(fd);
}

} // namespace readex_native_log

#endif // READEX_NATIVE_LOGGER_HPP
