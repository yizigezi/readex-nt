// test_book.cpp — Book Manager：打开/查询/关闭/生命周期（PR-3，计划书 §3.3/§七）
//
// 覆盖：错误路径（空路径/不存在/目录）、元信息、章节内容与原文一致、
// 索引越界、close 后访问报 InvalidHandle、重复关闭、多书并行不串扰、
// 无章节文本、GBK/UTF-16 端到端、大文件不开全本（RSS 检查）、fd 不泄漏。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"
#include "test_support.hpp"

#include "readex/book.hpp"

#include <iconv.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using readex::Book;
using readex::BookInfo;
using readex::ChapterInfo;
using readex::Encoding;
using readex::ErrorCode;
using readex::Result;
using readex::openBook;

namespace {

// UTF-8 -> 指定编码（iconv，仅测试素材生成）
std::string encodeTo(const std::string& utf8, const char* toName)
{
    iconv_t cd = iconv_open(toName, "UTF-8");
    if (cd == (iconv_t)-1)
        return std::string();
    std::string out;
    const char* in = utf8.data();
    size_t inLeft = utf8.size();
    char buf[4096];
    while (inLeft > 0) {
        char* outp = buf;
        size_t outLeft = sizeof(buf);
        if (iconv(cd, const_cast<char**>(&in), &inLeft, &outp, &outLeft) ==
            (size_t)-1) {
            iconv_close(cd);
            return std::string();
        }
        out.append(buf, sizeof(buf) - outLeft);
    }
    iconv_close(cd);
    return out;
}

// 当前进程 RSS（/proc/self/statm 第二列，单位页）；不可读返回 0
long rssPages()
{
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (f == nullptr)
        return 0;
    long total = 0, resident = 0;
    int n = std::fscanf(f, "%ld %ld", &total, &resident);
    std::fclose(f);
    return n == 2 ? resident : 0;
}

// 写大文件：每章一行标题 + 填充行，总大小约 bytes（不整体驻留内存）
std::string writeBigBook(const std::string& name, size_t bytes,
                         std::vector<std::string>* chapterStarts)
{
    std::string path = testsup::tmpDir() + "/" + name;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return path;
    const std::string filler(254, 'x');
    size_t written = 0;
    int chap = 0;
    while (written < bytes) {
        char title[64];
        std::snprintf(title, sizeof(title), "第%d章 大文件章节标题\n", ++chap);
        std::fputs(title, f);
        if (chapterStarts != nullptr)
            chapterStarts->push_back(std::string(title));
        written += std::strlen(title);
        for (int i = 0; i < 8 && written < bytes; ++i) {
            std::fputs(filler.c_str(), f);
            std::fputc('\n', f);
            written += filler.size() + 1;
        }
    }
    std::fclose(f);
    return path;
}

} // namespace

TEST(book_open_invalid_path)
{
    Result<Book> r1 = openBook("");
    CHECK(!r1.ok());
    if (!r1.ok())
        CHECK(r1.error().code() == ErrorCode::InvalidArgument);

    Result<Book> r2 = openBook(testsup::tmpDir() + "/missing_book.txt");
    CHECK(!r2.ok());
    if (!r2.ok())
        CHECK(r2.error().code() == ErrorCode::NotFound);

    Result<Book> r3 = openBook(testsup::tmpDir()); // 目录
    CHECK(!r3.ok());
    if (!r3.ok())
        CHECK(r3.error().code() == ErrorCode::IoError);
}

TEST(book_info_and_chapters_utf8)
{
    const std::string raw =
        "书名行\n作者：某\n\n第一章 初见\n正文一\n第二章 再见\n正文二";
    std::string path = testsup::writeTestFile("book_utf8.txt", raw);
    Result<Book> res = openBook(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    Book book = std::move(res.value());

    Result<BookInfo> info = book.getInfo();
    CHECK(info.ok());
    if (info.ok()) {
        CHECK_EQ(info.value().title, std::string("book_utf8")); // 文件名去扩展名
        CHECK_EQ(info.value().path, path);
        CHECK_EQ(info.value().sizeBytes, (uint64_t)raw.size());
        CHECK(info.value().encoding == Encoding::Utf8);
        CHECK_EQ(info.value().chapterCount, (size_t)3); // 前言 + 2 章
    }

    Result<std::vector<ChapterInfo>> chs = book.getChapters();
    CHECK(chs.ok());
    if (!chs.ok())
        return;
    CHECK_EQ(chs.value().size(), (size_t)3);
    if (chs.value().size() != 3)
        return;
    CHECK_EQ(chs.value()[1].title, std::string("第一章 初见"));
    CHECK_EQ(chs.value()[2].title, std::string("第二章 再见"));

    // 章节内容与原文一致（区间解码 == 对应原文）
    Result<std::string> t0 = book.getChapter(0);
    CHECK(t0.ok());
    if (t0.ok())
        CHECK_EQ(t0.value(), std::string("书名行\n作者：某\n\n"));
    Result<std::string> t1 = book.getChapter(1);
    CHECK(t1.ok());
    if (t1.ok())
        CHECK_EQ(t1.value(), std::string("第一章 初见\n正文一\n"));
    Result<std::string> t2 = book.getChapter(2);
    CHECK(t2.ok());
    if (t2.ok())
        CHECK_EQ(t2.value(), std::string("第二章 再见\n正文二"));

    // 索引越界 -> NotFound
    Result<std::string> t99 = book.getChapter(99);
    CHECK(!t99.ok());
    if (!t99.ok())
        CHECK(t99.error().code() == ErrorCode::NotFound);

    book.close();
    testsup::removeTestFile(path);
}

TEST(book_close_then_access_is_invalid_handle)
{
    std::string path = testsup::writeTestFile("book_closed.txt",
                                              "第一章\n正文\n");
    Result<Book> res = openBook(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    Book book = std::move(res.value());

    book.close();
    CHECK(!book.isOpen());

    CHECK(!book.getInfo().ok());
    CHECK(!book.getChapters().ok());
    CHECK(!book.getChapter(0).ok());
    Result<std::string> r = book.getChapter(0);
    if (!r.ok())
        CHECK(r.error().code() == ErrorCode::InvalidHandle);

    // 重复关闭安全
    book.close();
    book.close();
    CHECK(!book.getInfo().ok());

    testsup::removeTestFile(path);
}

TEST(book_multiple_books_no_crosstalk)
{
    std::string pathA = testsup::writeTestFile(
        "book_a.txt", "第一章 A1\n甲一\n第二章 A2\n甲二\n");
    std::string pathB = testsup::writeTestFile(
        "book_b.txt", "第一章 B1\n乙一\n第二章 B2\n乙二\n第三章 B3\n乙三\n");

    Result<Book> ra = openBook(pathA);
    Result<Book> rb = openBook(pathB);
    CHECK(ra.ok());
    CHECK(rb.ok());
    if (!ra.ok() || !rb.ok())
        return;
    Book a = std::move(ra.value());
    Book b = std::move(rb.value());

    CHECK_EQ(a.getInfo().value().chapterCount, (size_t)2);
    CHECK_EQ(b.getInfo().value().chapterCount, (size_t)3);

    // 交替读取，内容互不串扰
    CHECK_EQ(a.getChapter(0).value(), std::string("第一章 A1\n甲一\n"));
    CHECK_EQ(b.getChapter(1).value(), std::string("第二章 B2\n乙二\n"));
    CHECK_EQ(a.getChapter(1).value(), std::string("第二章 A2\n甲二\n"));
    CHECK_EQ(b.getChapter(2).value(), std::string("第三章 B3\n乙三\n"));
    CHECK_EQ(b.getChapter(0).value(), std::string("第一章 B1\n乙一\n"));

    // 关闭 A 不影响 B
    a.close();
    CHECK(!a.isOpen());
    CHECK(b.isOpen());
    CHECK_EQ(b.getChapter(0).value(), std::string("第一章 B1\n乙一\n"));

    b.close();
    testsup::removeTestFile(pathA);
    testsup::removeTestFile(pathB);
}

TEST(book_no_chapter_text_single_chapter)
{
    const std::string raw = "没有章节标题的纯文本。\n第二行。\n";
    std::string path = testsup::writeTestFile("book_plain.txt", raw);
    Result<Book> res = openBook(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    Book book = std::move(res.value());
    CHECK_EQ(book.getInfo().value().chapterCount, (size_t)1);
    Result<std::string> text = book.getChapter(0);
    CHECK(text.ok());
    if (text.ok())
        CHECK_EQ(text.value(), raw); // 整本单章，内容不丢
    book.close();
    testsup::removeTestFile(path);
}

TEST(book_gbk_end_to_end)
{
    const std::string u = "第一章 风雪夜归人\n正文甲。\n第二章 长亭外\n正文乙。";
    std::string raw = encodeTo(u, "GBK");
    CHECK(!raw.empty());
    std::string path = testsup::writeTestFile("book_gbk.txt", raw);
    Result<Book> res = openBook(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    Book book = std::move(res.value());
    CHECK(book.getInfo().value().encoding == Encoding::Gbk);
    CHECK_EQ(book.getInfo().value().chapterCount, (size_t)2);
    // getChapter 返回解码后的 UTF-8
    Result<std::string> t0 = book.getChapter(0);
    CHECK(t0.ok());
    if (t0.ok())
        CHECK_EQ(t0.value(), std::string("第一章 风雪夜归人\n正文甲。\n"));
    Result<std::string> t1 = book.getChapter(1);
    CHECK(t1.ok());
    if (t1.ok())
        CHECK_EQ(t1.value(), std::string("第二章 长亭外\n正文乙。"));
    book.close();
    testsup::removeTestFile(path);
}

TEST(book_utf16le_end_to_end)
{
    const std::string u = "第一章 初见\n正文一\n第二章\n正文二";
    std::string raw = std::string("\xFF\xFE", 2) + encodeTo(u, "UTF-16LE");
    CHECK(raw.size() > 2);
    std::string path = testsup::writeTestFile("book_u16.txt", raw);
    Result<Book> res = openBook(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    Book book = std::move(res.value());
    CHECK(book.getInfo().value().encoding == Encoding::Utf16Le);
    CHECK_EQ(book.getInfo().value().chapterCount, (size_t)2);
    Result<std::string> t1 = book.getChapter(1);
    CHECK(t1.ok());
    if (t1.ok())
        CHECK_EQ(t1.value(), std::string("第二章\n正文二"));
    book.close();
    testsup::removeTestFile(path);
}

TEST(book_large_file_not_loaded_whole)
{
    // ~16MiB 书：openBook 不得整本驻留内存（RSS 增量 << 文件大小），
    // 章节偏移正确、按需回读单章内容正确。
    const size_t kSize = 16 * 1024 * 1024;
    std::string path = writeBigBook("book_big.txt", kSize, nullptr);

    long before = rssPages();
    Result<Book> res = openBook(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    long after = rssPages();
    Book book = std::move(res.value());

    CHECK(book.getInfo().value().sizeBytes >= kSize);
    const size_t nChapters = book.getInfo().value().chapterCount;
    CHECK(nChapters > 100); // 每 ~2KiB 一章

    if (before > 0 && after > 0) {
        long deltaPages = after - before;
        long deltaBytes = deltaPages * 4096;
        // 16MiB 文件，RSS 增量须远小于文件大小（阈值 4MiB，留 allocator 余量）。
        // ASan 下跳过：sanitizer 的影子内存/隔离区/红区让 RSS 度量失效，
        // 该断言只在普通构建下有意义。
#if defined(__SANITIZE_ADDRESS__)
        (void)deltaBytes;
#else
        CHECK(deltaBytes < 4 * 1024 * 1024);
#endif
    }

    // 抽查中间章节内容：标题行 + 8 行填充
    Result<std::string> mid = book.getChapter(nChapters / 2);
    CHECK(mid.ok());
    if (mid.ok()) {
        const std::string& text = mid.value();
        CHECK(text.find("章 大文件章节标题\n") != std::string::npos);
        CHECK(text.find('x') != std::string::npos);
    }

    book.close();
    testsup::removeTestFile(path);
}

TEST(book_no_fd_leak)
{
    std::string path = testsup::writeTestFile("book_fd.txt", "第一章\n正文\n");
    int before = testsup::openFdCount();
    for (int i = 0; i < 20; ++i) {
        Result<Book> res = openBook(path);
        CHECK(res.ok());
        if (res.ok()) {
            Book book = std::move(res.value());
            (void)book.getChapter(0);
            book.close();
        }
    }
    int after = testsup::openFdCount();
    if (before >= 0 && after >= 0)
        CHECK_EQ(after, before);
    testsup::removeTestFile(path);
}

TEST(book_moved_state_transfers)
{
    std::string path =
        testsup::writeTestFile("book_move.txt", "第一章\n正文\n");
    Result<Book> res = openBook(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    Book a = std::move(res.value());
    Book b = std::move(a);
    CHECK(!a.isOpen()); // 移出后源对象不可用
    CHECK(b.isOpen());
    CHECK(b.getChapter(0).ok());
    CHECK(!a.getInfo().ok());
    b.close();
    testsup::removeTestFile(path);
}
