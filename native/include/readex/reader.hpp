// reader.hpp — 流式文件读取（PR-2，计划书 §3.1 File System）
//
// 与计划书建议接口的对应关系（语义不变，返回值改用 Result 表达错误）：
//   bool open(path)          -> Result<void> open(path)
//   size_t read(buf, size)   -> Result<size_t> read(buf, size)（值 = 实际读到的字节数）
//   bool seek(offset)        -> Result<void> seek(offset)
//   uint64_t tell()/size()   -> 不会失败，直接返回值
//   void close()             -> 幂等，析构自动调用（RAII）
//
// 设计要点：
//   - 64 位偏移（内部 open64/lseek64 语义，_FILE_OFFSET_BITS=64），支持大文件；
//     不提供整读接口，禁止把整本书读进内存——大文本一律分块 read。
//   - 错误路径映射到统一 ErrorCode：文件不存在 -> NotFound，
//     无权限 -> PermissionDenied，其余读写失败（含“路径是目录”）-> IoError；
//     在未打开的 FileReader 上 read/seek -> InvalidArgument。
//   - 句柄 RAII：析构/移动自动管理 fd，重复 close 安全，不泄漏 fd。
//   - POSIX open(O_RDONLY)：目录 open(2) 本身能成功，但读目录无意义，
//     故 open() 里 fstat 预检，是目录直接报 IoError，不让错误潜伏到 read()。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_READER_HPP
#define READEX_READER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "readex/error.hpp"

namespace readex {

class FileReader {
public:
    FileReader() noexcept;
    ~FileReader();

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    FileReader(FileReader&& other) noexcept;
    FileReader& operator=(FileReader&& other) noexcept;

    // 打开文件（只读）。若已打开其他文件，先自动 close 再打开新文件。
    Result<void> open(const std::string& path);

    bool isOpen() const noexcept;

    // 读取至多 size 字节到 buffer，返回实际读取字节数：
    //   - 返回值 < size 表示接近 EOF，返回 0 表示已到 EOF（不是错误）。
    //   - buffer == nullptr 且 size > 0 -> InvalidArgument。
    Result<size_t> read(char* buffer, size_t size);

    // 定位到绝对字节偏移（POSIX 语义，允许越过 EOF）。
    Result<void> seek(uint64_t offset);

    // 当前字节偏移 / 文件总字节数（打开时 fstat 缓存）。
    // 未打开时均返回 0，不会失败。
    uint64_t tell() const noexcept;
    uint64_t size() const noexcept;

    // 关闭文件；幂等，析构自动调用。
    void close() noexcept;

private:
    int fd_;          // -1 = 未打开
    uint64_t pos_;    // 当前偏移（随 read/seek 更新，tell() 用）
    uint64_t size_;   // 文件字节数（open 时 fstat 缓存）
};

} // namespace readex

#endif // READEX_READER_HPP
