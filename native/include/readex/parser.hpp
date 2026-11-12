// parser.hpp — TXT 章节解析（PR-3，计划书 §3.4）
//
// 设计约定：
//   1. 规则与解析逻辑分离：章节识别规则集中在 ChapterRule 规则表
//      （defaultChapterRules() 一处维护），匹配器（matchChapterTitle）与
//      文件扫描器（scanChapters）都只消费规则表，不内嵌任何规则。
//   2. 章节偏移一律是**原始文件字节偏移**（startOffset 含 / endOffset 不含），
//      正文读取时用 FileReader.seek 回读再解码（decoder 不提供偏移映射）。
//   3. 扫描器在字节层做编码感知的行切分（UTF-8/GBK 按 0x0A、UTF-16 按
//      0x000A 码元，CR-LF 一并剥离），行内字节不会被多字节字符跨界切断
//      （UTF-8/GBK 的多字节序列不可能包含 0x0A；UTF-16 行边界即码元边界）。
//      标题判定在**逐行解码后的 UTF-8 行文本**上做：每个短行的原始字节
//      （不含换行符）喂入同一个持久 TextDecoder，取该行的解码输出匹配规则。
//      这样解码正确性（跨块/状态）由 PR-2 解码器保证，偏移由字节扫描保证。
//      已知无害局限（不崩溃、不影响偏移正确性）：
//        - 文件里出现「多字节字符被换行符切断」的非法序列时，跨行可能有
//          1 个字符的解码串扰（仅影响该行标题判定）；
//        - 超过 kMaxTitleRawBytes 的超长行跳过标题判定（正文行本就不是标题），
//          其字节仍喂入解码器以保持流状态连续。
//   4. 无章节文本 → 整本单章（title = fallbackTitle，区间 [0, size)）；
//      识别失败不会抛异常、不会崩溃，最差退化为单章。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_PARSER_HPP
#define READEX_PARSER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "readex/decoder.hpp"
#include "readex/error.hpp"

namespace readex {

class FileReader; // 见 reader.hpp

// 章节元数据（计划书 §3.4 结构，字段语义不变）。
// 章节区间 [startOffset, endOffset) 首尾相接、无缝无重地覆盖全书字节。
// 标题章节的 startOffset 指向标题行首字节；文件开头标题行之前的非空白内容
// 会成为第 0 章（title = 书名/fallbackTitle）；纯空白前缀并入第一个章节
// （即该章 startOffset = 0），避免出现只含空白的垃圾章节。
struct ChapterInfo {
    std::string title;
    uint64_t startOffset = 0; // 原始文件字节偏移（含）
    uint64_t endOffset = 0;   // 原始文件字节偏移（不含）
    size_t index = 0;
};

// 章节识别规则（与核心解析逻辑分离，可配置注入）。
// 两种形态覆盖计划书 §3.4 的要求（第X章/第X节/Chapter X/序章/楔子/番外…）：
struct ChapterRule {
    enum class Kind {
        // 「前缀 + 数字 + 后缀（+ 标题余部）」：第X章 / 第X节 / Chapter X ……
        // 数字支持阿拉伯数字（含全角）与中文数字（一二三十百千万零两……）。
        Numbered,
        // 「关键词（+ 标题余部）」：序章 / 楔子 / 番外 / 尾声 / 后记 ……
        Keyword,
    };

    Kind kind = Kind::Keyword;
    std::string prefix;  // Numbered：数字前缀，如 "第" / "Chapter"
    std::string suffix;  // Numbered：数字后缀，如 "章" / "节"，可为空（Chapter X）
    std::string keyword; // Keyword：关键词
    bool allowSubtitle = true;    // 允许「分隔符 + 标题余部」，如 "第一章：初见"
    bool allowDirectTail = true;  // 允许标题余部直接相连（无分隔符），如
                                  // "第十二章风雪夜归人"；为抑制正文误判，
                                  // 仅对「整行很短」（≤ kMaxDirectTailCps 个
                                  // 码点）的行生效，见 parser.cpp。
    bool caseInsensitive = true;  // 前缀/后缀里的 ASCII 字母忽略大小写（Chapter）
};

// —— 标题行判定的公共约束（对所有规则生效，实现见 parser.cpp）——
// 行文本先剥首尾空白（含全角空格/U+FEFF），且整行原始字节 ≤ kMaxTitleRawBytes
// （超长行必为正文，跳过）才进入匹配。命中后标题 = 剥空白后的整行原文。
// 标题余部（pattern 之后的部分）首字符必须是「非词字」（空白/标点）或中文数字，
// 否则需要 allowDirectTail 且整行足够短——这是「第一章鱼」「第一节车厢」类
// 正文防误判的主要闸门；残余误判（如短行「第一章鱼」）为已知局限。

// 默认规则表（唯一维护点）：第X章/节/回/卷/篇/部/集/幕、Chapter X、
// 序章/序/序言/前言/引子/楔子/番外/番外篇/尾声/后记/附录/终章/终卷/大结局。
const std::vector<ChapterRule>& defaultChapterRules();

// 解码后 UTF-8 行文本（不含换行符）的标题判定；命中返回 true，
// outTitle（可空）写剥空白后的整行标题。纯函数，可在单测里直接驱动规则表。
bool matchChapterTitle(const std::string& lineUtf8,
                       const std::vector<ChapterRule>& rules,
                       std::string* outTitle = nullptr);

// 在原始字节流上扫描章节索引（顺序读全文件，但只保留章节元数据，不驻留正文；
// 结束后恢复 reader 进入时的位置）。
//   - 找不到任何章节 → 返回单章 [0, size)（title = fallbackTitle）
//   - 空文件 → 返回单章 [0, 0)
//   - 失败码：IoError（读失败）/ DecodeError（解码器建立失败，如缺 GBK gconv）
Result<std::vector<ChapterInfo>> scanChapters(FileReader& reader,
                                              Encoding encoding,
                                              const std::string& fallbackTitle);

// 同上，但注入自定义规则表（规则与解析逻辑分离的入口）。
Result<std::vector<ChapterInfo>> scanChapters(FileReader& reader,
                                              Encoding encoding,
                                              const std::string& fallbackTitle,
                                              const std::vector<ChapterRule>& rules);

} // namespace readex

#endif // READEX_PARSER_HPP
