// test_parser.cpp — 章节解析：标题匹配 + 字节偏移扫描（PR-3，计划书 §3.4/§七）
//
// 覆盖：中文小说常见标题（第X章/节/回/卷、Chapter X、序章/楔子/番外…）、
// 特殊标题防崩（句末标点正文行、超长行、非法字节）、无章节文本、空文件、
// CRLF、GBK / UTF-16 章节偏移、自定义规则注入。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"
#include "test_support.hpp"

#include "readex/parser.hpp"
#include "readex/reader.hpp"

#include <iconv.h>

#include <cstdint>
#include <string>
#include <vector>

using readex::ChapterInfo;
using readex::ChapterRule;
using readex::Encoding;
using readex::FileReader;
using readex::Result;
using readex::matchChapterTitle;
using readex::scanChapters;

namespace {

// UTF-8 -> 指定编码的测试素材转换（iconv，仅测试用；运行环境与 decoder 的
// GBK 路径同样依赖 glibc iconv，属测试环境既有依赖）
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

std::string toGbk(const std::string& utf8) { return encodeTo(utf8, "GBK"); }
std::string toUtf16Le(const std::string& utf8)
{
    return encodeTo(utf8, "UTF-16LE");
}
std::string toUtf16Be(const std::string& utf8)
{
    return encodeTo(utf8, "UTF-16BE");
}

// 打开临时文件并扫描
Result<std::vector<ChapterInfo>> scan(const std::string& raw, Encoding enc,
                                      const char* name,
                                      const std::vector<ChapterRule>* rules = nullptr)
{
    std::string path = testsup::writeTestFile(name, raw);
    FileReader r;
    Result<void> o = r.open(path);
    if (!o.ok())
        return Result<std::vector<ChapterInfo>>::failure(o.error());
    Result<std::vector<ChapterInfo>> res =
        rules ? scanChapters(r, enc, "书名", *rules)
              : scanChapters(r, enc, "书名");
    testsup::removeTestFile(path);
    return res;
}

bool matchDefault(const std::string& line)
{
    return matchChapterTitle(line, readex::defaultChapterRules());
}

} // namespace

// ---- 标题匹配：中文小说常见标题 ----

TEST(parser_match_common_cn_titles)
{
    // 第X章 各种数字形态
    CHECK(matchDefault("第一章"));
    CHECK(matchDefault("第1章"));
    CHECK(matchDefault("第12章 风雪夜归人"));
    CHECK(matchDefault("第十二章 风雪夜归人"));
    CHECK(matchDefault("第一千零二十四章"));
    CHECK(matchDefault("第一百零二章 风雪夜归人（上）"));
    CHECK(matchDefault("第2012章了不起的开始")); // 无空格短标题
    CHECK(matchDefault("第一章：初见"));
    CHECK(matchDefault("第 12 章")); // 数字两侧空格
    // 其它序数词
    CHECK(matchDefault("第12节 概述"));
    CHECK(matchDefault("第三回 宴桃园豪杰三结义"));
    CHECK(matchDefault("第2卷 风起"));
    CHECK(matchDefault("第十篇 远行"));
    // 英文章节
    CHECK(matchDefault("Chapter 1"));
    CHECK(matchDefault("CHAPTER 12 The Return"));
    CHECK(matchDefault("chapter3"));
    // 关键词标题
    CHECK(matchDefault("序章"));
    CHECK(matchDefault("楔子"));
    CHECK(matchDefault("番外"));
    CHECK(matchDefault("番外篇"));
    CHECK(matchDefault("番外 双胞胎"));
    CHECK(matchDefault("番外一 起源")); // 中文数字余部
    CHECK(matchDefault("尾声"));
    CHECK(matchDefault("后记"));
    CHECK(matchDefault("前言"));
    CHECK(matchDefault("大结局"));
    CHECK(matchDefault("终章"));
    // 缩进与全角空格
    CHECK(matchDefault("  第一章 初见"));
    CHECK(matchDefault("\u3000第一章 初见"));
}

TEST(parser_match_extracts_trimmed_title)
{
    std::string title;
    CHECK(matchChapterTitle("  第十二章 风雪夜归人  ",
                            readex::defaultChapterRules(), &title));
    CHECK_EQ(title, std::string("第十二章 风雪夜归人"));
    CHECK(matchChapterTitle("楔子", readex::defaultChapterRules(), &title));
    CHECK_EQ(title, std::string("楔子"));
}

// ---- 标题匹配：正文防误判 / 特殊标题防崩 ----

TEST(parser_no_match_body_lines)
{
    CHECK(!matchDefault(""));
    CHECK(!matchDefault("   \t  "));
    CHECK(!matchDefault("他翻到第一章的末尾。"));     // 不以标题开头
    CHECK(!matchDefault("第章"));                     // 无数字
    CHECK(!matchDefault("第A章"));                    // 非数字
    CHECK(!matchDefault("Chapter"));                  // 无数字
    CHECK(!matchDefault("序章的由来"));               // 关键词保守策略
    CHECK(!matchDefault("附录A"));                    // 同上（防「番外的…」类误判）
    CHECK(!matchDefault("第一章的内容并不复杂，只是我忘了")); // 直连余部超短行限制
    // 句末标点结尾 = 正文句子（G1 闸门）
    CHECK(!matchDefault("第一章鱼。"));
    CHECK(!matchDefault("第一章的内容并不复杂，只是我忘了。"));
    CHECK(!matchDefault("楔子……"));
    // 超长标题行按正文处理
    std::string longTitle = "第一章 ";
    longTitle += std::string(300, 'x'); // 304 码点，超标题上限
    CHECK(!matchDefault(longTitle));
}

TEST(parser_known_limitation_short_body_line)
{
    // 已知局限（见 parser.hpp 头注释）：极短正文行可能误判，与主流阅读器
    // 行为一致；此处锁死当前行为，若未来收紧规则再更新本用例。
    CHECK(matchDefault("第一章鱼"));
}

TEST(parser_match_custom_rules_injectable)
{
    // 自定义规则集：只认「卷X …」（默认规则不认）
    ChapterRule r;
    r.kind = ChapterRule::Kind::Numbered;
    r.prefix = "卷";
    r.suffix = "";
    std::vector<ChapterRule> rules{ r };

    CHECK(matchChapterTitle("卷一 风起", rules));
    CHECK(!matchChapterTitle("卷一 风起", readex::defaultChapterRules()));
    // 自定义规则同样受正文闸门约束
    CHECK(!matchChapterTitle("卷一的传说，很长。", rules));
    // 空规则集不命中任何行
    CHECK(!matchChapterTitle("第一章 初见", std::vector<ChapterRule>()));
}

// ---- 扫描器：UTF-8 字节偏移 ----

TEST(parser_scan_utf8_offsets_and_slices)
{
    const std::string t0 = "书名行\n作者：某\n\n";
    const std::string t1 = "第一章 初见\n正文一\n";
    const std::string t2 = "第二章 再见\n正文二\n";
    const std::string raw = t0 + t1 + t2;

    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf8, "scan_utf8.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    const std::vector<ChapterInfo>& chs = res.value();
    // 标题前有非空白内容 -> 第 0 章（书名）+ 两个标题章节
    CHECK_EQ(chs.size(), (size_t)3);
    if (chs.size() != 3)
        return;
    CHECK_EQ(chs[0].title, std::string("书名"));
    CHECK_EQ(chs[0].startOffset, (uint64_t)0);
    CHECK_EQ(chs[0].endOffset, (uint64_t)t0.size());
    CHECK_EQ(chs[1].title, std::string("第一章 初见"));
    CHECK_EQ(chs[1].startOffset, (uint64_t)t0.size());
    CHECK_EQ(chs[1].endOffset, (uint64_t)(t0.size() + t1.size()));
    CHECK_EQ(chs[2].title, std::string("第二章 再见"));
    CHECK_EQ(chs[2].startOffset, (uint64_t)(t0.size() + t1.size()));
    CHECK_EQ(chs[2].endOffset, (uint64_t)raw.size());
    // 索引递增；区间无缝覆盖全书
    for (size_t i = 0; i < chs.size(); ++i) {
        CHECK_EQ(chs[i].index, i);
        if (i > 0)
            CHECK_EQ(chs[i - 1].endOffset, chs[i].startOffset);
    }
    // 偏移能正确定位原文：切片 == 原始字节区间
    CHECK_EQ(raw.substr(chs[1].startOffset, chs[1].endOffset - chs[1].startOffset),
             t1);
    CHECK_EQ(raw.substr(chs[2].startOffset, chs[2].endOffset - chs[2].startOffset),
             t2);
}

TEST(parser_scan_whitespace_preamble_folds_into_first_chapter)
{
    const std::string raw = "\n\n  \n第一章 初见\n正文\n";
    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf8, "scan_fold.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    const std::vector<ChapterInfo>& chs = res.value();
    CHECK_EQ(chs.size(), (size_t)1);
    if (chs.size() != 1)
        return;
    CHECK_EQ(chs[0].title, std::string("第一章 初见"));
    CHECK_EQ(chs[0].startOffset, (uint64_t)0); // 空白前缀并入
    CHECK_EQ(chs[0].endOffset, (uint64_t)raw.size());
}

TEST(parser_scan_title_at_offset_zero)
{
    const std::string raw = "第一章\n正文\n";
    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf8, "scan_zero.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    CHECK_EQ(res.value().size(), (size_t)1);
    if (res.value().size() == 1) {
        CHECK_EQ(res.value()[0].title, std::string("第一章"));
        CHECK_EQ(res.value()[0].startOffset, (uint64_t)0);
    }
}

TEST(parser_scan_no_chapter_text_is_single_chapter)
{
    const std::string raw = "普通文本第一行\n第二行，没有任何章节标题。\n第三行\n";
    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf8, "scan_plain.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    const std::vector<ChapterInfo>& chs = res.value();
    CHECK_EQ(chs.size(), (size_t)1);
    if (chs.size() != 1)
        return;
    CHECK_EQ(chs[0].title, std::string("书名")); // fallback = 书名
    CHECK_EQ(chs[0].startOffset, (uint64_t)0);
    CHECK_EQ(chs[0].endOffset, (uint64_t)raw.size());
}

TEST(parser_scan_empty_file_single_empty_chapter)
{
    Result<std::vector<ChapterInfo>> res = scan("", Encoding::Utf8, "scan_empty.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    CHECK_EQ(res.value().size(), (size_t)1);
    if (res.value().size() == 1) {
        CHECK_EQ(res.value()[0].startOffset, (uint64_t)0);
        CHECK_EQ(res.value()[0].endOffset, (uint64_t)0);
    }
}

TEST(parser_scan_crlf_line_endings)
{
    const std::string l1 = "第一章 初见\r\n";
    const std::string l2 = "正文一\r\n";
    const std::string l3 = "第二章\r\n";
    const std::string raw = l1 + l2 + l3;
    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf8, "scan_crlf.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    const std::vector<ChapterInfo>& chs = res.value();
    // 第一章区间包含自己的正文行（「正文一」），直到第二章标题行
    CHECK_EQ(chs.size(), (size_t)2);
    if (chs.size() != 2)
        return;
    CHECK_EQ(chs[0].title, std::string("第一章 初见"));
    CHECK_EQ(chs[0].startOffset, (uint64_t)0);
    CHECK_EQ(chs[0].endOffset, (uint64_t)(l1.size() + l2.size()));
    CHECK_EQ(chs[1].title, std::string("第二章"));
    CHECK_EQ(chs[1].startOffset, (uint64_t)(l1.size() + l2.size()));
    CHECK_EQ(chs[1].endOffset, (uint64_t)raw.size());
}

// ---- 扫描器：GBK / UTF-16 偏移（原始字节）----

TEST(parser_scan_gbk_offsets)
{
    const std::string u1 = "第一章 风雪夜归人\n正文甲。\n";
    const std::string u2 = "第二章 长亭外\n正文乙。\n";
    const std::string raw = toGbk(u1) + toGbk(u2);
    CHECK(!raw.empty()); // 转换失败则素材为空，后续断言自然失败

    Result<std::vector<ChapterInfo>> res = scan(raw, Encoding::Gbk, "scan_gbk.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    const std::vector<ChapterInfo>& chs = res.value();
    CHECK_EQ(chs.size(), (size_t)2);
    if (chs.size() != 2)
        return;
    CHECK_EQ(chs[0].title, std::string("第一章 风雪夜归人"));
    CHECK_EQ(chs[0].startOffset, (uint64_t)0);
    CHECK_EQ(chs[0].endOffset, (uint64_t)toGbk(u1).size());
    CHECK_EQ(chs[1].title, std::string("第二章 长亭外"));
    CHECK_EQ(chs[1].startOffset, (uint64_t)toGbk(u1).size());
    CHECK_EQ(chs[1].endOffset, (uint64_t)raw.size());
    // 字节切片 == 原始 GBK 字节
    CHECK_EQ(raw.substr(chs[1].startOffset), toGbk(u2));
}

TEST(parser_scan_utf16le_offsets_with_bom)
{
    const std::string u1 = "第一章 初见\n正文一\n";
    const std::string u2 = "第二章\n正文二\n";
    const std::string bom("\xFF\xFE", 2);
    const std::string raw = bom + toUtf16Le(u1) + toUtf16Le(u2);
    CHECK(raw.size() > 2);

    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf16Le, "scan_u16le.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    const std::vector<ChapterInfo>& chs = res.value();
    CHECK_EQ(chs.size(), (size_t)2);
    if (chs.size() != 2)
        return;
    CHECK_EQ(chs[0].title, std::string("第一章 初见"));
    CHECK_EQ(chs[0].startOffset, (uint64_t)0); // BOM 属于第一章区间
    CHECK_EQ(chs[0].endOffset, (uint64_t)(bom.size() + toUtf16Le(u1).size()));
    CHECK_EQ(chs[1].title, std::string("第二章"));
    CHECK_EQ(chs[1].startOffset, (uint64_t)(bom.size() + toUtf16Le(u1).size()));
    CHECK_EQ(chs[1].endOffset, (uint64_t)raw.size());
}

TEST(parser_scan_utf16be_offsets)
{
    const std::string u1 = "第一章\n甲\n";
    const std::string u2 = "第二章\n乙\n";
    const std::string raw = toUtf16Be(u1) + toUtf16Be(u2);
    CHECK(!raw.empty());

    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf16Be, "scan_u16be.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    const std::vector<ChapterInfo>& chs = res.value();
    CHECK_EQ(chs.size(), (size_t)2);
    if (chs.size() != 2)
        return;
    CHECK_EQ(chs[0].endOffset, (uint64_t)toUtf16Be(u1).size());
    CHECK_EQ(chs[1].startOffset, (uint64_t)toUtf16Be(u1).size());
}

// ---- 扫描器：特殊输入防崩 ----

TEST(parser_scan_special_titles_do_not_crash)
{
    // 特殊/畸形标题混排 + 非法字节 + 超长行 + 无换行结尾
    std::string raw;
    raw += "\xEF\xBB\xBF第一章\n";          // BOM + 标题
    raw += "正文\x80\x81 非法字节混入。\n";   // 非法 UTF-8（解码为 U+FFFD）
    raw += "第章\n第A章\nChapter\n";          // 伪标题
    raw += "第一章：\n";                      // 空标题
    raw += "第";                              // 行尾半个字符前缀（下一行拼接）
    raw += "二章 星夜\n";
    raw += "第一章 " + std::string(4096, 'x') + "\n"; // 超长行
    raw += "尾声";                            // 无换行结尾

    Result<std::vector<ChapterInfo>> res =
        scan(raw, Encoding::Utf8, "scan_special.txt");
    CHECK(res.ok());
    if (!res.ok())
        return;
    // BOM 行「第一章」+ 第一行尾「第」与下一行拼接的「二章 星夜」不算
    // （跨行拼接），「第一章：」、「尾声」命中；共 3 个标题章节 + 前言？
    // 第一行就是标题 -> 无前言章。断言只取稳健部分：
    const std::vector<ChapterInfo>& chs = res.value();
    CHECK(chs.size() >= 3);
    CHECK(chs.size() <= 5);
    // 无论如何区间必须无缝覆盖全书
    CHECK_EQ(chs.front().startOffset, (uint64_t)0);
    CHECK_EQ(chs.back().endOffset, (uint64_t)raw.size());
    for (size_t i = 1; i < chs.size(); ++i)
        CHECK_EQ(chs[i - 1].endOffset, chs[i].startOffset);
}

TEST(parser_scan_custom_rules_used_by_scanner)
{
    ChapterRule r;
    r.kind = ChapterRule::Kind::Keyword;
    r.keyword = "特别篇";
    std::vector<ChapterRule> rules{ r };
    const std::string raw = "特别篇 一\n甲\n特别篇 二\n乙\n";

    std::string path = testsup::writeTestFile("scan_custom.txt", raw);
    FileReader f;
    CHECK(f.open(path).ok());
    Result<std::vector<ChapterInfo>> res =
        scanChapters(f, Encoding::Utf8, "书名", rules);
    testsup::removeTestFile(path);
    CHECK(res.ok());
    if (!res.ok())
        return;
    CHECK_EQ(res.value().size(), (size_t)2);
}
