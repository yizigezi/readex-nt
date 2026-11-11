// book.hpp — 书籍生命周期管理接口（PR-3 实现）
//
// 当前仅接口声明，无实现。任务范围见计划书 §3.3：
//   - 打开/查询/关闭书籍；多本书同时打开不串扰；重复关闭不崩溃
//   - Book 对象不把内部文件指针 / C++ 地址暴露给 JavaScript，
//     JS 侧只拿受管理句柄，对象释放时执行清理
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_BOOK_HPP
#define READEX_BOOK_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "readex/decoder.hpp"
#include "readex/error.hpp"

namespace readex {

struct ChapterInfo; // 见 parser.hpp（PR-3）

struct BookInfo {
    std::string title;
    std::string path;
    uint64_t sizeBytes = 0;
    Encoding encoding; // 见 decoder.hpp，PR-2 引入后生效
};

class Book {
public:
    // 打开失败返回对应 ErrorCode（NotFound / PermissionDenied / IoError / DecodeError）。
    // 注意：不能用 Book::open 静态成员返回 Result<Book>（声明处 Book 尚不完整），
    // 统一用自由函数 openBook，语义相同。

    BookInfo getInfo() const;
    std::vector<ChapterInfo> getChapters() const;

    // 按章节索引取正文（章节识别失败的纯文本按单章处理）。
    Result<std::string> getChapter(size_t index) const;

    void close(); // 幂等：重复关闭安全
};

// 打开书籍（PR-3 实现）。失败码：NotFound / PermissionDenied / IoError / DecodeError。
Result<Book> openBook(const std::string& path);

} // namespace readex

#endif // READEX_BOOK_HPP
