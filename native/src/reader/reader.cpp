// reader.cpp — 流式文件读取实现（PR-2）
//
// SPDX-License-Identifier: GPL-3.0

// 32 位平台（armhf）上 off_t 默认 32 位：开 LFS 让 open/lseek/fstat 走
// *64 变体，保证 64 位偏移支持 >2GiB 文件（这些符号 glibc 2.23 早已有）。
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "readex/reader.hpp"

#include <cerrno>
#include <cstring>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace readex {
namespace {

Error errnoToError(int err, const char* op, const std::string& path)
{
    ErrorCode code;
    switch (err) {
    case ENOENT:
        code = ErrorCode::NotFound;
        break;
    case EACCES:
    case EPERM:
        code = ErrorCode::PermissionDenied;
        break;
    default:
        code = ErrorCode::IoError;
        break;
    }
    return Error(code, std::string(op) + " '" + path + "': " + std::strerror(err));
}

} // namespace

FileReader::FileReader() noexcept : fd_(-1), pos_(0), size_(0) {}

FileReader::~FileReader()
{
    close();
}

FileReader::FileReader(FileReader&& other) noexcept
    : fd_(other.fd_), pos_(other.pos_), size_(other.size_)
{
    other.fd_ = -1;
    other.pos_ = 0;
    other.size_ = 0;
}

FileReader& FileReader::operator=(FileReader&& other) noexcept
{
    if (this == &other)
        return *this;
    close();
    fd_ = other.fd_;
    pos_ = other.pos_;
    size_ = other.size_;
    other.fd_ = -1;
    other.pos_ = 0;
    other.size_ = 0;
    return *this;
}

Result<void> FileReader::open(const std::string& path)
{
    close(); // 重复 open：自动关闭先前句柄

    if (path.empty())
        return Result<void>::failure(ErrorCode::InvalidArgument, "open: 路径为空");

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return Result<void>::failure(errnoToError(errno, "open", path));

    struct stat st;
    if (::fstat(fd, &st) != 0) {
        int err = errno;
        ::close(fd);
        return Result<void>::failure(errnoToError(err, "fstat", path));
    }
    if (S_ISDIR(st.st_mode)) {
        ::close(fd);
        return Result<void>::failure(ErrorCode::IoError,
                                     "open '" + path + "': 是目录，不能读取");
    }

    fd_ = fd;
    pos_ = 0;
    size_ = static_cast<uint64_t>(st.st_size);
    return Result<void>::success();
}

bool FileReader::isOpen() const noexcept
{
    return fd_ >= 0;
}

Result<size_t> FileReader::read(char* buffer, size_t size)
{
    if (fd_ < 0)
        return Result<size_t>::failure(ErrorCode::InvalidArgument,
                                       "read: 文件未打开");
    if (buffer == nullptr && size > 0)
        return Result<size_t>::failure(ErrorCode::InvalidArgument,
                                       "read: buffer 为空");
    if (size == 0)
        return Result<size_t>::success(0);

    ssize_t n;
    do {
        n = ::read(fd_, buffer, size);
    } while (n < 0 && errno == EINTR);

    if (n < 0)
        return Result<size_t>::failure(ErrorCode::IoError,
                                       std::string("read: ") + std::strerror(errno));

    pos_ += static_cast<uint64_t>(n);
    return Result<size_t>::success(static_cast<size_t>(n));
}

Result<void> FileReader::seek(uint64_t offset)
{
    if (fd_ < 0)
        return Result<void>::failure(ErrorCode::InvalidArgument,
                                     "seek: 文件未打开");

    off_t off = static_cast<off_t>(offset);
    if (static_cast<uint64_t>(off) != offset)
        return Result<void>::failure(ErrorCode::InvalidArgument,
                                     "seek: 偏移超出平台范围");

    off_t r = ::lseek(fd_, off, SEEK_SET);
    if (r == static_cast<off_t>(-1))
        return Result<void>::failure(ErrorCode::IoError,
                                     std::string("seek: ") + std::strerror(errno));

    pos_ = static_cast<uint64_t>(r);
    return Result<void>::success();
}

uint64_t FileReader::tell() const noexcept
{
    return pos_;
}

uint64_t FileReader::size() const noexcept
{
    return size_;
}

void FileReader::close() noexcept
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    pos_ = 0;
    size_ = 0;
}

} // namespace readex
