// search.hpp — 全文搜索接口（PR-5 实现）
//
// 当前仅接口声明，无实现。任务范围见计划书 §3.6：
//   - 第一阶段用简单可靠的字符串搜索，不做过早的索引系统
//   - 返回匹配偏移与所在章节；支持限制结果数量
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_SEARCH_HPP
#define READEX_SEARCH_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace readex {

struct SearchResult {
    size_t chapterIndex = 0;
    uint64_t offset = 0; // 章节内偏移（具体基准在 PR-5 实现时定）
    std::string context; // 匹配处前后文摘录
};

class SearchEngine {
public:
    static std::vector<SearchResult> search(
        const std::string& text,
        const std::string& keyword,
        size_t limit);
};

} // namespace readex

#endif // READEX_SEARCH_HPP
