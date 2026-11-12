// search.cpp — 全文搜索实现（PR-5，计划书 §3.6）
//
// 第一阶段朴素字符串搜索（std::string::find，逐章、命中 limit 即停），不做索引。
// 语义与偏差说明见 search.hpp 头注释。不依赖 QuickJS。
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/search.hpp"

#include "readex/book.hpp"

#include <string>
#include <utility>
#include <vector>

namespace readex {
namespace {

// 把 [0, size) 内的字节位置 p 吸附到码点边界：
//   snapBackward（起点用）：p 落在多字节字符中间时退到该字符首字节；
//   snapForward（终点用）：p 落在多字节字符中间时进到该字符之后。
// UTF-8 续字节形如 10xxxxxx。文本本身若是非法 UTF-8（解码层已保证不会），
// 吸附仍按字节推进、不越界、不崩。
size_t snapBackward(const std::string& text, size_t p)
{
    while (p > 0 && p < text.size() &&
           (static_cast<unsigned char>(text[p]) & 0xC0u) == 0x80u)
        --p;
    return p;
}

size_t snapForward(const std::string& text, size_t p)
{
    while (p < text.size() &&
           (static_cast<unsigned char>(text[p]) & 0xC0u) == 0x80u)
        ++p;
    return p;
}

} // namespace

Result<std::vector<SearchResult>> SearchEngine::searchText(
    const std::string& text,
    const std::string& keyword,
    size_t limit,
    const SearchOptions& options,
    size_t chapterIndex)
{
    if (keyword.empty())
        return Result<std::vector<SearchResult>>::failure(
            ErrorCode::InvalidArgument, "search: 关键词不能为空");
    if (limit == 0)
        return Result<std::vector<SearchResult>>::failure(
            ErrorCode::InvalidArgument,
            "search: limit 须为正数（限流上限；不限量请传大值）");

    std::vector<SearchResult> results;
    results.reserve(limit < 64 ? limit : 64); // 有界预分配，避免大 limit 狂占内存

    size_t pos = 0;
    while (results.size() < limit) {
        size_t p = text.find(keyword, pos);
        if (p == std::string::npos)
            break;

        // context：匹配前后各取预算字节，边界吸附码点边界，首尾自然截断
        size_t begin = snapBackward(
            text, p >= options.contextBefore ? p - options.contextBefore : 0);
        size_t end = p + keyword.size() + options.contextAfter;
        if (end >= text.size() || end < p + keyword.size()) // 后者防溢出
            end = text.size();
        end = snapForward(text, end);

        SearchResult r;
        r.chapterIndex = chapterIndex;
        r.offset = static_cast<uint64_t>(p);
        r.context = text.substr(begin, end - begin);
        results.push_back(std::move(r));

        pos = p + keyword.size(); // 匹配互不重叠，从上一匹配末尾继续
    }

    return Result<std::vector<SearchResult>>::success(std::move(results));
}

Result<std::vector<SearchResult>> SearchEngine::search(
    const Book& book,
    const std::string& keyword,
    size_t limit,
    const SearchOptions& options)
{
    if (keyword.empty())
        return Result<std::vector<SearchResult>>::failure(
            ErrorCode::InvalidArgument, "search: 关键词不能为空");
    if (limit == 0)
        return Result<std::vector<SearchResult>>::failure(
            ErrorCode::InvalidArgument,
            "search: limit 须为正数（限流上限；不限量请传大值）");

    Result<std::vector<ChapterInfo>> chapters = book.getChapters();
    if (!chapters.ok())
        return Result<std::vector<SearchResult>>::failure(chapters.error());

    std::vector<SearchResult> results;
    results.reserve(limit < 64 ? limit : 64);
    for (size_t i = 0; i < chapters.value().size(); ++i) {
        if (results.size() >= limit)
            break; // 命中 limit 即停（分章 + 限流保证工作量有界）
        Result<std::string> text = book.getChapter(i);
        if (!text.ok())
            return Result<std::vector<SearchResult>>::failure(text.error());
        Result<std::vector<SearchResult>> part = SearchEngine::searchText(
            text.value(), keyword, limit - results.size(), options, i);
        if (!part.ok())
            return part;
        for (SearchResult& r : part.value())
            results.push_back(std::move(r));
    }
    return Result<std::vector<SearchResult>>::success(std::move(results));
}

} // namespace readex
