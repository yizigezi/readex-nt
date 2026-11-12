// book.hpp — 书籍生命周期管理（PR-3，计划书 §3.3）
//
// 与计划书建议接口的对应关系（语义不变，返回值统一走 Result 表达错误）：
//   BookInfo getInfo()                -> Result<BookInfo> getInfo()
//   std::vector<ChapterInfo> get…     -> Result<std::vector<ChapterInfo>> get…()
//   std::string getChapter(size_t)    -> Result<std::string> getChapter(size_t)
//   Book::open(path)（Book 尚不完整，不能静态返回 Result<Book>）
//                                        -> 自由函数 openBook(path)
// getInfo/getChapters 也用 Result 包装，是为了让「close 后继续访问」能统一
// 返回明确错误（InvalidHandle），而不是靠未定义的空对象/空列表蒙混过关。
//
// 设计要点：
//   - 生命周期严格：close() 幂等（重复关闭安全）；close 后任何访问返回
//     InvalidHandle；Book 只可移动不可复制（RAII，析构自动 close）。
//   - getChapter 用 FileReader.seek 回读 [startOffset, endOffset) 再解码
//     （PR-2 约定：章节扫描保留原始字节偏移，decoder 不提供偏移映射），
//     64KiB 分块流式解码，不把整本书驻留内存。
//   - 多本书互不串扰：每本 Book 独立持有自己的 FileReader 与章节索引。
//   - C++ 对象地址不暴露给 JavaScript；JS 侧经受管理句柄表
//     （book_registry.hpp）访问，对象释放时执行清理。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_BOOK_HPP
#define READEX_BOOK_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "readex/decoder.hpp"
#include "readex/error.hpp"
#include "readex/parser.hpp" // ChapterInfo
#include "readex/reader.hpp"

namespace readex {

struct BookInfo {
    std::string title;    // 书名 = 文件名（去扩展名）；TXT 无内嵌书名
    std::string path;     // 打开时的路径
    uint64_t sizeBytes = 0;
    Encoding encoding = Encoding::Unknown;
    size_t chapterCount = 0;
};

class Book {
public:
    ~Book();
    Book(Book&& other) noexcept;
    Book& operator=(Book&& other) noexcept;
    Book(const Book&) = delete;
    Book& operator=(const Book&) = delete;

    // 以下访问在 close() 之后一律返回 InvalidHandle 错误。
    Result<BookInfo> getInfo() const;
    Result<std::vector<ChapterInfo>> getChapters() const;

    // 按章节索引取正文（解码为 UTF-8，保留章节区间原文，含标题行）。
    // 越界 -> NotFound；章节识别失败的纯文本按单章处理（index 0）。
    // 内部 seek + 64KiB 分块流式解码，不驻留全书。
    Result<std::string> getChapter(size_t index) const;

    void close() noexcept; // 幂等：重复关闭安全，析构自动调用
    bool isOpen() const noexcept;

private:
    Book() noexcept;
    friend Result<Book> openBook(const std::string& path);

    std::string path_;
    BookInfo info_;
    std::vector<ChapterInfo> chapters_;
    mutable FileReader reader_; // getChapter 为 const，但需 seek/read
    bool open_ = false;
};

// 打开书籍：open 文件 -> detect 编码 -> scanChapters 章节扫描。
// 失败码：InvalidArgument（空路径）/ NotFound / PermissionDenied / IoError /
// DecodeError（如缺 GBK gconv 模块）。章节规则用默认规则表
// （parser.hpp defaultChapterRules）。
Result<Book> openBook(const std::string& path);

} // namespace readex

#endif // READEX_BOOK_HPP
