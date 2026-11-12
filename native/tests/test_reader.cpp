// test_reader.cpp — FileReader 流式读取（PR-2，计划书 §3.1/§七 文件读取）
//
// 覆盖：不存在文件 / 无权限 / 目录 / 大文件分块读取 / seek·tell·size /
// EOF 语义 / 重复打开 / 移动语义 / fd 不泄漏。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"
#include "test_support.hpp"

#include "readex/reader.hpp"

#include <cstdint>
#include <string>
#include <vector>

using readex::ErrorCode;
using readex::FileReader;
using readex::Result;

namespace {

// 大文件测试素材：byte(i) = (i*31 + 7) & 0xFF，可就地校验，不占额外内存
const size_t kPatternSize = 5 * 1024 * 1024;

std::string makePattern(size_t n)
{
    std::string s;
    s.reserve(n);
    for (size_t i = 0; i < n; ++i)
        s += static_cast<char>((i * 31 + 7) & 0xFF);
    return s;
}

unsigned char patternByte(uint64_t i)
{
    return static_cast<unsigned char>((i * 31 + 7) & 0xFF);
}

} // namespace

TEST(reader_open_missing_file_is_not_found)
{
    FileReader r;
    Result<void> res = r.open(testsup::tmpDir() + "/definitely_missing_file.bin");
    CHECK(!res.ok());
    CHECK(res.error().code() == ErrorCode::NotFound);
    CHECK(!r.isOpen());
}

TEST(reader_open_empty_path_is_invalid_argument)
{
    FileReader r;
    Result<void> res = r.open("");
    CHECK(!res.ok());
    CHECK(res.error().code() == ErrorCode::InvalidArgument);
}

TEST(reader_open_directory_is_io_error)
{
    FileReader r;
    Result<void> res = r.open(testsup::tmpDir());
    CHECK(!res.ok());
    CHECK(res.error().code() == ErrorCode::IoError);
    CHECK(!r.isOpen());
}

TEST(reader_open_permission_denied)
{
    if (::geteuid() == 0) {
        // root 不受权限位约束，无法验证该路径
        return;
    }
    std::string path = testsup::writeTestFile("perm_denied.bin", "secret");
    ::chmod(path.c_str(), 0000);

    FileReader r;
    Result<void> res = r.open(path);
    CHECK(!res.ok());
    CHECK(res.error().code() == ErrorCode::PermissionDenied);

    ::chmod(path.c_str(), 0644);
    testsup::removeTestFile(path);
}

TEST(reader_read_empty_file)
{
    std::string path = testsup::writeTestFile("empty.bin", "");
    FileReader r;
    CHECK(r.open(path).ok());
    CHECK_EQ(r.size(), uint64_t(0));
    CHECK_EQ(r.tell(), uint64_t(0));

    char buf[16];
    Result<size_t> n = r.read(buf, sizeof buf);
    CHECK(n.ok());
    CHECK_EQ(n.value(), size_t(0)); // EOF 不是错误

    r.close();
    testsup::removeTestFile(path);
}

TEST(reader_large_file_chunked_read_offsets)
{
    std::string path = testsup::writeTestFile("pattern_5mb.bin", makePattern(kPatternSize));

    FileReader r;
    CHECK(r.open(path).ok());
    CHECK_EQ(r.size(), uint64_t(kPatternSize));

    // 8KiB 分块顺序读，逐字节校验 + tell() 偏移校验
    const size_t kChunk = 8 * 1024;
    std::vector<char> buf(kChunk);
    uint64_t offset = 0;
    for (;;) {
        Result<size_t> n = r.read(buf.data(), buf.size());
        CHECK(n.ok());
        if (!n.ok())
            break;
        size_t got = n.value();
        if (got == 0)
            break;
        for (size_t i = 0; i < got; ++i) {
            if (static_cast<unsigned char>(buf[i]) != patternByte(offset + i)) {
                CHECK(false); // 偏移/内容错位
                break;
            }
        }
        offset += got;
        CHECK_EQ(r.tell(), offset);
    }
    CHECK_EQ(offset, uint64_t(kPatternSize));

    // 随机位置 seek 后读取校验
    const uint64_t probes[] = { 0, 1, 4095, 65536, 1234567, 3141592,
                                kPatternSize - 16, kPatternSize - 1 };
    for (uint64_t off : probes) {
        CHECK(r.seek(off).ok());
        CHECK_EQ(r.tell(), off);
        unsigned char b = 0;
        Result<size_t> n = r.read(reinterpret_cast<char*>(&b), 1);
        CHECK(n.ok());
        CHECK_EQ(n.value(), size_t(1));
        CHECK_EQ(int(b), int(patternByte(off)));
    }

    // seek 到 EOF 处读取返回 0；越过 EOF 是合法 seek（POSIX 语义）
    CHECK(r.seek(kPatternSize).ok());
    char one;
    Result<size_t> n = r.read(&one, 1);
    CHECK(n.ok());
    CHECK_EQ(n.value(), size_t(0));

    r.close();
    testsup::removeTestFile(path);
}

TEST(reader_read_small_request_near_eof_is_short_read)
{
    std::string path = testsup::writeTestFile("short.bin", "abcdefghij");
    FileReader r;
    CHECK(r.open(path).ok());
    CHECK(r.seek(7).ok());

    char buf[16];
    Result<size_t> n = r.read(buf, sizeof buf);
    CHECK(n.ok());
    CHECK_EQ(n.value(), size_t(3)); // 只剩 3 字节
    CHECK(std::string(buf, 3) == "hij");

    r.close();
    testsup::removeTestFile(path);
}

TEST(reader_operations_on_closed_file_are_invalid_argument)
{
    FileReader r;
    char buf[4];
    Result<size_t> n = r.read(buf, sizeof buf);
    CHECK(!n.ok());
    CHECK(n.error().code() == ErrorCode::InvalidArgument);
    Result<void> s = r.seek(0);
    CHECK(!s.ok());
    CHECK(s.error().code() == ErrorCode::InvalidArgument);
    CHECK_EQ(r.tell(), uint64_t(0));
    CHECK_EQ(r.size(), uint64_t(0));

    r.close(); // 重复 close 幂等
    r.close();
    CHECK(!r.isOpen());
}

TEST(reader_null_buffer_is_invalid_argument)
{
    std::string path = testsup::writeTestFile("nullbuf.bin", "x");
    FileReader r;
    CHECK(r.open(path).ok());
    Result<size_t> n = r.read(nullptr, 8);
    CHECK(!n.ok());
    CHECK(n.error().code() == ErrorCode::InvalidArgument);
    // size 为 0 的读取不碰 buffer
    Result<size_t> z = r.read(nullptr, 0);
    CHECK(z.ok());
    CHECK_EQ(z.value(), size_t(0));
    r.close();
    testsup::removeTestFile(path);
}

TEST(reader_reopen_closes_previous)
{
    std::string pathA = testsup::writeTestFile("reopen_a.bin", "AAAA");
    std::string pathB = testsup::writeTestFile("reopen_b.bin", "BBBBBB");

    FileReader r;
    CHECK(r.open(pathA).ok());
    CHECK(r.open(pathB).ok()); // 自动关掉 A
    CHECK_EQ(r.size(), uint64_t(6));
    char buf[8];
    Result<size_t> n = r.read(buf, sizeof buf);
    CHECK(n.ok());
    CHECK_EQ(std::string(buf, n.value()), std::string("BBBBBB"));

    r.close();
    testsup::removeTestFile(pathA);
    testsup::removeTestFile(pathB);
}

TEST(reader_move_semantics)
{
    std::string path = testsup::writeTestFile("move.bin", "12345");
    FileReader a;
    CHECK(a.open(path).ok());

    FileReader b(std::move(a)); // NOLINT(performance-move-const-arg) — 故意测移动
    CHECK(b.isOpen());
    CHECK_EQ(b.size(), uint64_t(5));
    CHECK(!a.isOpen()); // 移动后源对象安全（析构不 double-close）

    FileReader c;
    c = std::move(b);
    CHECK(c.isOpen());
    CHECK(!b.isOpen());
    char buf[8];
    Result<size_t> n = c.read(buf, sizeof buf);
    CHECK(n.ok());
    CHECK_EQ(std::string(buf, n.value()), std::string("12345"));

    c.close();
    testsup::removeTestFile(path);
}

TEST(reader_fd_not_leaked)
{
    std::string path = testsup::writeTestFile("fd_leak.bin", "leak-check");
    int before = testsup::openFdCount();
    CHECK(before > 0); // /proc/self/fd 可用

    for (int i = 0; i < 500; ++i) {
        FileReader r;
        CHECK(r.open(path).ok());
        char buf[4];
        (void)r.read(buf, sizeof buf);
        // 不显式 close，靠析构回收
    }
    for (int i = 0; i < 500; ++i) {
        FileReader r;
        CHECK(r.open(path).ok());
        r.close();
    }

    int after = testsup::openFdCount();
    CHECK_EQ(after, before); // 反复开关后 fd 计数不涨

    testsup::removeTestFile(path);
}
