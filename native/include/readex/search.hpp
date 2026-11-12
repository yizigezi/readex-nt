// search.hpp — 全文搜索接口（PR-5，计划书 §3.6）
//
// —— 偏移语义（与 pagination.hpp / reading_state.hpp 同一把尺子，勿改）——
//   SearchResult.offset 是「匹配处所在章节**已解码文本**（UTF-8）内的字节偏移」，
//   恒落在码点边界；与 PageInfo.startOffset / ReadingState.textOffset 完全同语义，
//   可直接与页区间比对定位所在页。不是原始文件字节偏移（那是 ChapterInfo 的语义）。
//
// —— 第一阶段：朴素字符串搜索（计划书明确：不做过早的索引系统）——
//   - 关键词按**字节子串**匹配（UTF-8 文本内 find）；有效 UTF-8 关键词的匹配
//     必然起止于码点边界（UTF-8 自同步性：首字节不可能是续字节）；
//   - 匹配互不重叠、每章内从左到右、跨章按章节顺序；命中 limit 立即停止
//     （分章搜索 + limit 限流保证工作量有界，同步 API 可接受）；
//   - 无匹配返回空列表（不是错误）。
//
// —— context（前后文摘录）——
//   匹配前后各取一段原文（字节预算可配，默认各 32 字节），预算边界**吸附到
//   码点边界**（起点向后退到字符首、终点向前进到字符尾，绝不切开多字节字符），
//   文本首尾自然截断。context 是纯原文子串，不含省略号等附加符号。
//
// —— 与计划书 §3.6 建议接口的偏差及理由 ——
//   1. 返回 Result<std::vector<SearchResult>>（计划书为裸 vector）：空关键词、
//      limit==0 等参数非法走统一 Error 体系（InvalidArgument），Book 关闭后
//      搜索报 InvalidHandle，不靠空列表蒙混——与 book.hpp/pagination.hpp 同约定。
//   2. search 增加 Book 重载（计划书的 search(keyword, limit) 隐含整书搜索）：
//      searchText 是单章文本搜索（纯函数、可单测），search(book, ...) 逐章
//      getChapter + searchText 组合，命中 limit 即停。
//   3. SearchOptions 收纳 context 长度（任务要求「长度可配/有默认值」）。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_SEARCH_HPP
#define READEX_SEARCH_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "readex/error.hpp"

namespace readex {

class Book; // 搜索不持有书，只读（book.hpp 在 search.cpp 内包含）

struct SearchResult {
    size_t chapterIndex = 0;
    uint64_t offset = 0; // 章内已解码文本（UTF-8）字节偏移（码点边界）
    std::string context; // 匹配处前后原文摘录（码点边界截取）
};

// 搜索选项：context 前后字节预算（0 = 该侧不取前后文）
struct SearchOptions {
    size_t contextBefore = 32;
    size_t contextAfter = 32;
};

class SearchEngine {
public:
    // 单章文本搜索（text 为一章的已解码 UTF-8 文本）。
    // keyword 为空 -> InvalidArgument；limit == 0 -> InvalidArgument（限流上限
    // 须为正数）；无匹配 -> 空列表。结果里 chapterIndex = 传入的 chapterIndex。
    static Result<std::vector<SearchResult>> searchText(
        const std::string& text,
        const std::string& keyword,
        size_t limit,
        const SearchOptions& options = SearchOptions(),
        size_t chapterIndex = 0);

    // 整书搜索：逐章 getChapter + searchText，按章节顺序累积，命中 limit 即停。
    // Book 已 close -> InvalidHandle（getChapter/getChapters 的错误原样透传）。
    static Result<std::vector<SearchResult>> search(
        const Book& book,
        const std::string& keyword,
        size_t limit,
        const SearchOptions& options = SearchOptions());
};

} // namespace readex

#endif // READEX_SEARCH_HPP
