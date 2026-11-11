// parser.hpp — TXT 书籍/章节解析接口（PR-3 实现）
//
// 当前仅接口声明，无实现。任务范围见计划书 §3.4：
//   - 章节识别规则（第X章/第X节/Chapter X/序章/楔子/番外…）与解析逻辑分离，
//     规则可配置、不硬编码散落
//   - 保留章节在原始文件中的字节偏移；无章节标题的纯文本也能打开
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_PARSER_HPP
#define READEX_PARSER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace readex {

struct ChapterInfo {
    std::string title;
    uint64_t startOffset = 0; // 原始文件字节偏移（含）
    uint64_t endOffset = 0;   // 原始文件字节偏移（不含）
    size_t index = 0;
};

// 章节识别规则（与核心解析逻辑分离，可配置注入）。
struct ChapterRule {
    std::string pattern; // 具体正则/匹配语法在 PR-3 实现时确定
};

class BookParser {
public:
    // 对已解码文本做章节切分；识别不到章节时返回单章（整书一章）。
    static std::vector<ChapterInfo> parseChapters(
        const std::string& text,
        const std::vector<ChapterRule>& rules);
};

} // namespace readex

#endif // READEX_PARSER_HPP
