// book.cpp — 书籍生命周期管理实现（PR-3，计划书 §3.3）
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/book.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace readex {
namespace {

// 取文件名（去目录、去扩展名）作为书名（TXT 无内嵌书名）
std::string titleFromPath(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0)
        name = name.substr(0, dot);
    return name;
}

const size_t kReadChunk = 64 * 1024;

} // namespace

Book::Book() noexcept {}
Book::~Book() { close(); }

Book::Book(Book&& other) noexcept
    : path_(std::move(other.path_))
    , info_(std::move(other.info_))
    , chapters_(std::move(other.chapters_))
    , reader_(std::move(other.reader_))
    , open_(other.open_)
{
    other.open_ = false;
}

Book& Book::operator=(Book&& other) noexcept
{
    if (this == &other)
        return *this;
    close();
    path_ = std::move(other.path_);
    info_ = std::move(other.info_);
    chapters_ = std::move(other.chapters_);
    reader_ = std::move(other.reader_);
    open_ = other.open_;
    other.open_ = false;
    return *this;
}

bool Book::isOpen() const noexcept
{
    return open_;
}

void Book::close() noexcept
{
    reader_.close();
    open_ = false;
}

Result<BookInfo> Book::getInfo() const
{
    if (!open_)
        return Result<BookInfo>::failure(ErrorCode::InvalidHandle,
                                         "Book::getInfo: 书籍已关闭");
    return Result<BookInfo>::success(info_);
}

Result<std::vector<ChapterInfo>> Book::getChapters() const
{
    if (!open_)
        return Result<std::vector<ChapterInfo>>::failure(
            ErrorCode::InvalidHandle, "Book::getChapters: 书籍已关闭");
    return Result<std::vector<ChapterInfo>>::success(chapters_);
}

Result<std::string> Book::getChapter(size_t index) const
{
    if (!open_)
        return Result<std::string>::failure(ErrorCode::InvalidHandle,
                                            "Book::getChapter: 书籍已关闭");
    if (index >= chapters_.size())
        return Result<std::string>::failure(
            ErrorCode::NotFound, "Book::getChapter: 章节索引越界");

    const ChapterInfo& ch = chapters_[index];
    Result<void> sres = reader_.seek(ch.startOffset);
    if (!sres.ok())
        return Result<std::string>::failure(sres.error());

    Result<TextDecoder> dres = TextDecoder::create(info_.encoding);
    if (!dres.ok())
        return Result<std::string>::failure(dres.error());
    TextDecoder decoder = std::move(dres.value());

    // 64KiB 分块流式解码 [startOffset, endOffset)，不驻留原始字节全量
    std::string out;
    uint64_t remaining = ch.endOffset - ch.startOffset;
    std::string chunk(kReadChunk, '\0');
    while (remaining > 0) {
        const size_t want = static_cast<size_t>(
            std::min<uint64_t>(remaining, kReadChunk));
        Result<size_t> rres = reader_.read(&chunk[0], want);
        if (!rres.ok())
            return Result<std::string>::failure(rres.error());
        const size_t got = rres.value();
        if (got == 0)
            break; // 文件被截短：返回已解码部分
        out += decoder.feed(chunk.data(), got);
        remaining -= got;
    }
    out += decoder.finish();
    return Result<std::string>::success(std::move(out));
}

Result<Book> openBook(const std::string& path)
{
    if (path.empty())
        return Result<Book>::failure(ErrorCode::InvalidArgument,
                                     "openBook: 路径为空");

    Book book;
    book.path_ = path;

    Result<void> ores = book.reader_.open(path);
    if (!ores.ok())
        return Result<Book>::failure(ores.error());

    Result<Encoding> dres = TextDecoder::detect(book.reader_);
    if (!dres.ok()) {
        book.close();
        return Result<Book>::failure(dres.error());
    }
    book.info_.encoding = dres.value();

    Result<std::vector<ChapterInfo>> cres =
        scanChapters(book.reader_, book.info_.encoding, titleFromPath(path));
    if (!cres.ok()) {
        book.close();
        return Result<Book>::failure(cres.error());
    }
    book.chapters_ = std::move(cres.value());

    book.info_.title = titleFromPath(path);
    book.info_.path = path;
    book.info_.sizeBytes = book.reader_.size();
    book.info_.chapterCount = book.chapters_.size();
    book.open_ = true;
    return Result<Book>::success(std::move(book));
}

} // namespace readex
