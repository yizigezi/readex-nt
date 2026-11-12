// test_pagination.cpp — 分页引擎（PR-4，计划书 §3.5/§七）
//
// 覆盖：黄金用例（确定行/页切分）、稳定性、无缝拼接性质（不丢字、不重复）、
// 中英混排、标点避头尾（行首禁则/行尾禁则）、英文词内不断、空/纯空白/
// CRLF/超长行/单字超宽/极小页、配置边界报错、重新分页、估算参数与自定义
// 测量器注入、分类与换行性质表、大文本耗时探针（记录用，不设虚假指标）。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"

#include "readex/pagination.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using readex::CharClass;
using readex::CharTraits;
using readex::classifyCodepoint;
using readex::codepointTraits;
using readex::Error;
using readex::ErrorCode;
using readex::EstimatingMeasurer;
using readex::EstimatorConfig;
using readex::PageInfo;
using readex::PaginationConfig;
using readex::PaginationEngine;
using readex::Result;
using readex::TextMeasurer;

namespace {

constexpr double kTol = 1e-3;

void checkTiling(const std::string& content, const std::vector<PageInfo>& pages,
                 const char* what)
{
    auto fail = [&](const char* msg) {
        ::microtest::reportFail(__FILE__, __LINE__,
                                std::string(what) + ": " + msg);
    };
    if (pages.empty()) {
        fail("页列表为空");
        return;
    }
    if (pages[0].startOffset != 0)
        fail("首页起点应为 0");
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].pageIndex != i)
            fail("pageIndex 应从 0 递增");
        if (pages[i].startOffset > pages[i].endOffset)
            fail("区间起点越过终点");
        if (pages[i].endOffset > content.size())
            fail("区间终点越过 content 末尾");
        // 偏移恒落在码点边界（不切字符）
        for (uint64_t off : { pages[i].startOffset, pages[i].endOffset }) {
            if (off < content.size()) {
                const unsigned char b =
                    static_cast<unsigned char>(content[static_cast<size_t>(off)]);
                if ((b & 0xC0u) == 0x80u)
                    fail("偏移落在多字节字符中间");
            }
        }
        if (i > 0 && pages[i].startOffset != pages[i - 1].endOffset)
            fail("页区间不连续（有缝隙或重叠）");
    }
    if (pages.back().endOffset != content.size())
        fail("末页终点应为 content.size()（拼接 == 原文）");
}

bool samePages(const std::vector<PageInfo>& a, const std::vector<PageInfo>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].startOffset != b[i].startOffset ||
            a[i].endOffset != b[i].endOffset || a[i].pageIndex != b[i].pageIndex)
            return false;
    }
    return true;
}

// 基础配置：字号 10（CJK=10px，ASCII=5px），行高 1.0，无边距/字距/段距
PaginationConfig baseCfg(float w, float h)
{
    PaginationConfig c;
    c.width = w;
    c.height = h;
    c.fontSize = 10;
    c.lineHeight = 1.0f;
    return c;
}

std::vector<PageInfo> mustPaginate(const std::string& content,
                                   const PaginationConfig& cfg,
                                   const char* what)
{
    Result<std::vector<PageInfo>> res = PaginationEngine::paginate(content, cfg);
    if (!res.ok()) {
        ::microtest::reportFail(__FILE__, __LINE__,
                                std::string(what) + ": 不应失败: " +
                                    res.error().toString());
        return {};
    }
    checkTiling(content, res.value(), what);
    return res.value();
}

// ---- 确定性伪随机素材（LCG，固定种子）----
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    uint32_t next()
    {
        s = s * 1664525u + 1013904223u;
        return s;
    }
};

std::string mixedText(size_t chars, uint32_t seed)
{
    // 「天地玄黄…」24 字（每字 3 字节）；「，。…」14 个标点（每字 3 字节）
    static const char han[] = "天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往秋收冬藏";
    static const char punct[] = "，。！？；：「」『』——……";
    static const char ascii[] = "abcdefghij XYZ 0123 "; // 20 字节
    std::string out;
    Lcg rng(seed);
    for (size_t i = 0; i < chars; ++i) {
        switch (rng.next() % 10) {
        case 0:
            out += '\n';
            break;
        case 1:
            out.append(punct + (rng.next() / 7) % 14 * 3, 3);
            break;
        case 2:
        case 3:
            out += ascii[(rng.next() / 3) % 20];
            break;
        default:
            out.append(han + (rng.next() / 5) % 24 * 3, 3);
            break;
        }
    }
    return out;
}

} // namespace

// ---- 黄金用例：行/页切分精确断言 ----

TEST(pagination_golden_basic)
{
    // "一二三四五六"：每行 2 字（行宽 25 装 2 个 10px 字），高 20 装 2 行
    const std::string content = "一二三四五六"; // 18 字节
    std::vector<PageInfo> pages = mustPaginate(content, baseCfg(25, 20), "basic");
    CHECK_EQ(pages.size(), 2u);
    if (pages.size() == 2) {
        CHECK_EQ(pages[0].startOffset, 0u);
        CHECK_EQ(pages[0].endOffset, 12u); // "一二三四"
        CHECK_EQ(pages[1].startOffset, 12u);
        CHECK_EQ(pages[1].endOffset, 18u); // "五六"
    }
    // 高 100：3 行同页
    pages = mustPaginate(content, baseCfg(25, 100), "basic-tall");
    CHECK_EQ(pages.size(), 1u);
    if (pages.size() == 1)
        CHECK_EQ(pages[0].endOffset, 18u);
}

TEST(pagination_golden_line_start_forbidden)
{
    // 句号不落行首：宽度够 3 字，"甲乙丙。" 若朴素断会把 。甩到行首；
    // 避头尾应提前断在「乙|丙」：行 = "甲乙" / "丙。"
    const std::string content = "甲乙丙。"; // 12 字节
    std::vector<PageInfo> pages =
        mustPaginate(content, baseCfg(35, 10), "punct-no-line-head");
    CHECK_EQ(pages.size(), 2u);
    if (pages.size() == 2) {
        CHECK_EQ(pages[0].endOffset, 6u);  // "甲乙"
        CHECK_EQ(pages[1].startOffset, 6u); // "丙。"
    }
}

TEST(pagination_golden_line_end_forbidden)
{
    // 引号不落行尾：宽度 2 字，"甲「乙丙" 若朴素断会把 「留在行尾；
    // 避头尾应断在「甲|「乙」：行 = "甲" / "「乙" / "丙"
    const std::string content = "甲「乙丙"; // 12 字节
    std::vector<PageInfo> pages =
        mustPaginate(content, baseCfg(20, 10), "quote-no-line-tail");
    CHECK_EQ(pages.size(), 3u);
    if (pages.size() == 3) {
        CHECK_EQ(pages[0].endOffset, 3u);   // "甲"
        CHECK_EQ(pages[1].startOffset, 3u); // "「乙"
        CHECK_EQ(pages[1].endOffset, 9u);
        CHECK_EQ(pages[2].startOffset, 9u); // "丙"
    }
}

TEST(pagination_golden_word_integrity)
{
    // 英文词内不断：换行点选在空白后，"abcde" 整词上移到下行
    const std::string content = "xy abcde"; // 8 字节，ASCII 5px/字，行宽 25
    std::vector<PageInfo> pages =
        mustPaginate(content, baseCfg(25, 10), "word-integrity");
    CHECK_EQ(pages.size(), 2u);
    if (pages.size() == 2) {
        CHECK_EQ(pages[0].endOffset, 3u);   // "xy "
        CHECK_EQ(pages[1].startOffset, 3u); // "abcde" 整词
    }
}

TEST(pagination_golden_letter_spacing)
{
    // letterSpacing=5：每字 15px，"一二三" 每行只装 1 字 -> 3 页（行高 10）
    const std::string content = "一二三"; // 9 字节
    PaginationConfig cfg = baseCfg(25, 10);
    cfg.letterSpacing = 5;
    std::vector<PageInfo> pages = mustPaginate(content, cfg, "letter-spacing");
    CHECK_EQ(pages.size(), 3u);
    // 无字距对照：2 行（"一二"/"三"）-> 2 页
    pages = mustPaginate(content, baseCfg(25, 10), "letter-spacing-none");
    CHECK_EQ(pages.size(), 2u);
}

TEST(pagination_golden_paragraph_spacing)
{
    // 段距把第二段挤到下一页（段距不跨页，页顶不补）
    const std::string content = "一\n二"; // 7 字节
    PaginationConfig cfg = baseCfg(100, 25);
    cfg.paragraphSpacing = 10;
    std::vector<PageInfo> pages = mustPaginate(content, cfg, "para-spacing");
    CHECK_EQ(pages.size(), 2u);
    if (pages.size() == 2) {
        CHECK_EQ(pages[0].endOffset, 4u);   // "一\n"
        CHECK_EQ(pages[1].startOffset, 4u); // "二"
    }
    // 无段距对照：两行同页
    pages = mustPaginate(content, baseCfg(100, 25), "para-spacing-none");
    CHECK_EQ(pages.size(), 1u);
}

TEST(pagination_golden_margins)
{
    // 左右边距 5+5：可用宽 20（2 字/行），与 width=20 等价
    const std::string content = "一二三";
    PaginationConfig cfg = baseCfg(30, 100);
    cfg.paddingLeft = 5;
    cfg.paddingRight = 5;
    std::vector<PageInfo> pages = mustPaginate(content, cfg, "margins");
    CHECK_EQ(pages.size(), 1u);
    if (pages.size() == 1)
        CHECK_EQ(pages[0].endOffset, 9u);
    // 同内容按 2 字/行应为两行；用行高 10 / 高 10 验证行数 = 页数
    cfg.height = 10;
    pages = mustPaginate(content, cfg, "margins-lines");
    CHECK_EQ(pages.size(), 2u);
}

// ---- 稳定性与无缝拼接性质 ----

TEST(pagination_stability)
{
    const std::string content = mixedText(2000, 42);
    PaginationConfig cfg = baseCfg(233, 91);
    cfg.letterSpacing = 1;
    cfg.paragraphSpacing = 3;
    std::vector<PageInfo> a = PaginationEngine::paginate(content, cfg).value();
    std::vector<PageInfo> b = PaginationEngine::paginate(content, cfg).value();
    CHECK(samePages(a, b));
}

TEST(pagination_seamless_property)
{
    // 性质校验：多种配置 × 伪随机中英混排素材，各页区间无缝拼接 == 原文
    const std::string content = mixedText(5000, 7);
    checkTiling(content, PaginationEngine::paginate(content, baseCfg(200, 300)).value(),
                "prop-1");
    PaginationConfig cfg2 = baseCfg(60, 40);
    cfg2.paragraphSpacing = 5;
    cfg2.letterSpacing = 2;
    checkTiling(content, PaginationEngine::paginate(content, cfg2).value(),
                "prop-2");
    PaginationConfig cfg3 = baseCfg(400, 12);
    cfg3.paddingTop = 3;
    cfg3.paddingLeft = 7;
    checkTiling(content, PaginationEngine::paginate(content, cfg3).value(),
                "prop-3");
    // 拼接性质：逐页 substr 拼回 == 原文
    std::vector<PageInfo> pages =
        PaginationEngine::paginate(content, cfg2).value();
    std::string joined;
    for (const PageInfo& p : pages)
        joined += content.substr(static_cast<size_t>(p.startOffset),
                                 static_cast<size_t>(p.endOffset - p.startOffset));
    CHECK(joined == content);
}

TEST(pagination_mixed_cjk_ascii)
{
    // 中英混排不崩且可拼接；含中文标点与英文标点
    const std::string content =
        "Hello，世界！This is a 混排 test，含标点「引号」与 (paren) 123。";
    std::vector<PageInfo> pages = mustPaginate(content, baseCfg(120, 50), "mixed");
    CHECK(!pages.empty());
    pages = mustPaginate(content, baseCfg(33, 13), "mixed-narrow");
    CHECK(!pages.empty());
}

// ---- 空 / 空白 / CRLF / 极端输入不崩 ----

TEST(pagination_empty_and_blank)
{
    // 空文本 -> 单个空页（UI 总有页可显示）
    std::vector<PageInfo> pages = mustPaginate("", baseCfg(100, 100), "empty");
    CHECK_EQ(pages.size(), 1u);
    if (pages.size() == 1) {
        CHECK_EQ(pages[0].startOffset, 0u);
        CHECK_EQ(pages[0].endOffset, 0u);
        CHECK_EQ(pages[0].pageIndex, 0u);
    }
    // 纯空白
    pages = mustPaginate("   \t  ", baseCfg(100, 100), "blank");
    CHECK_EQ(pages.size(), 1u);
    // 纯换行："\n\n\n" -> 3 个空行（尾部空段不补行）
    pages = mustPaginate("\n\n\n", baseCfg(100, 10), "newlines");
    CHECK_EQ(pages.size(), 3u);
    // 单个换行
    pages = mustPaginate("\n", baseCfg(100, 10), "newline-one");
    CHECK_EQ(pages.size(), 1u);
    if (pages.size() == 1)
        CHECK_EQ(pages[0].endOffset, 1u);
    // 尾随换行不补空行："abc\n" -> 1 行 1 页
    pages = mustPaginate("abc\n", baseCfg(100, 10), "trailing-nl");
    CHECK_EQ(pages.size(), 1u);
    if (pages.size() == 1)
        CHECK_EQ(pages[0].endOffset, 4u);
}

TEST(pagination_crlf)
{
    // \r\n 视为一个换行；偏移无缝覆盖换行字节
    const std::string content = "甲\r\n乙"; // 8 字节
    std::vector<PageInfo> pages = mustPaginate(content, baseCfg(100, 10), "crlf");
    CHECK_EQ(pages.size(), 2u);
    if (pages.size() == 2) {
        CHECK_EQ(pages[0].endOffset, 5u);   // "甲\r\n"
        CHECK_EQ(pages[1].startOffset, 5u); // "乙"
    }
    // 孤 \r 同样断行
    pages = mustPaginate("甲\r乙", baseCfg(100, 10), "cr");
    CHECK_EQ(pages.size(), 2u);
}

TEST(pagination_extreme_sizes)
{
    // 单字比可用行宽还宽：每字一行（允许溢出），不崩、推进、无缝
    std::vector<PageInfo> pages =
        mustPaginate("甲乙丙", baseCfg(5, 5), "tiny-width-height");
    CHECK_EQ(pages.size(), 3u);
    // 超长行（连续 CJK / 连续 ASCII 词 / 连续禁则标点）不崩
    std::string longHan;
    for (size_t i = 0; i < 1000; ++i)
        longHan += "汉";
    pages = mustPaginate(longHan, baseCfg(25, 15), "long-han");
    CHECK(pages.size() > 100);
    std::string longWord(2000, 'w');
    pages = mustPaginate(longWord, baseCfg(33, 20), "long-word");
    CHECK(pages.size() > 100);
    std::string longPunct;
    for (size_t i = 0; i < 500; ++i)
        longPunct += "。"; // 全是行首禁则字的病态输入
    pages = mustPaginate(longPunct, baseCfg(25, 15), "long-punct");
    CHECK(pages.size() > 50);
}

// ---- 配置边界：一律报 InvalidArgument，不崩不吞 ----

TEST(pagination_config_errors)
{
    auto expectInvalid = [](const PaginationConfig& cfg, const char* what) {
        Result<std::vector<PageInfo>> res =
            PaginationEngine::paginate("甲乙丙", cfg);
        if (res.ok()) {
            ::microtest::reportFail(__FILE__, __LINE__,
                                    std::string(what) + ": 应报错却成功了");
        } else if (res.error().code() != ErrorCode::InvalidArgument) {
            ::microtest::reportFail(__FILE__, __LINE__,
                                    std::string(what) + ": 错误码应为 InvalidArgument，得到 " +
                                        res.error().toString());
        }
    };
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();

    PaginationConfig c = baseCfg(100, 100);
    c.width = 0;
    expectInvalid(c, "width=0");
    c = baseCfg(100, 100);
    c.width = -5;
    expectInvalid(c, "width<0");
    c = baseCfg(100, 100);
    c.width = nan;
    expectInvalid(c, "width=NaN");
    c = baseCfg(100, 100);
    c.height = 0;
    expectInvalid(c, "height=0");
    c = baseCfg(100, 100);
    c.height = -1;
    expectInvalid(c, "height<0");
    c = baseCfg(100, 100);
    c.height = inf;
    expectInvalid(c, "height=inf");
    c = baseCfg(100, 100);
    c.fontSize = 0;
    expectInvalid(c, "fontSize=0");
    c = baseCfg(100, 100);
    c.fontSize = -2;
    expectInvalid(c, "fontSize<0");
    c = baseCfg(100, 100);
    c.lineHeight = 0;
    expectInvalid(c, "lineHeight=0");
    c = baseCfg(100, 100);
    c.letterSpacing = -0.5f;
    expectInvalid(c, "letterSpacing<0");
    c = baseCfg(100, 100);
    c.paragraphSpacing = -1;
    expectInvalid(c, "paragraphSpacing<0");
    c = baseCfg(100, 100);
    c.paddingTop = -1;
    expectInvalid(c, "paddingTop<0");
    c = baseCfg(100, 100);
    c.paddingLeft = 60;
    c.paddingRight = 50;
    expectInvalid(c, "左右边距吃光宽度");
    c = baseCfg(100, 100);
    c.paddingTop = 60;
    c.paddingBottom = 40;
    expectInvalid(c, "上下边距吃光高度");
    c = baseCfg(100, 100);
    c.estimator.asciiWidthEm = 0;
    expectInvalid(c, "asciiWidthEm=0");
    c = baseCfg(100, 100);
    c.estimator.cjkWidthEm = -1;
    expectInvalid(c, "cjkWidthEm<0");
    c = baseCfg(100, 100);
    c.estimator.punctuationWidthEm = nan;
    expectInvalid(c, "punctuationWidthEm=NaN");

    // 合法配置正常返回
    CHECK(PaginationEngine::paginate("甲乙丙", baseCfg(100, 100)).ok());
    // 单独的校验函数口径一致
    c = baseCfg(100, 100);
    CHECK(readex::validatePaginationConfig(c).ok());
    c.width = 0;
    CHECK(!readex::validatePaginationConfig(c).ok());
}

// ---- 重新分页：改配置重排，仍无缝 ----

TEST(pagination_repaging)
{
    std::string content;
    for (size_t i = 0; i < 60; ++i)
        content += "重排";
    std::vector<PageInfo> narrow = mustPaginate(content, baseCfg(20, 100), "re-n");
    std::vector<PageInfo> wide =
        mustPaginate(content, baseCfg(400, 100), "re-w");
    CHECK(narrow.size() > wide.size());
    CHECK(!samePages(narrow, wide));
    // 字号变化也参与重排
    PaginationConfig cfg = baseCfg(100, 50);
    cfg.fontSize = 30;
    std::vector<PageInfo> big = mustPaginate(content, cfg, "re-fs");
    CHECK(big.size() > mustPaginate(content, baseCfg(100, 50), "re-fs0").size());
}

// ---- 估算参数与测量解耦 ----

TEST(pagination_estimator_params)
{
    // 标点全宽 vs 半宽改变行切分："甲，乙" 行宽 25：
    //   全宽（，=10px）：20+10 超宽 -> "甲，" / "乙" 两页（行高 10）
    //   半宽（，=5px）：15+10=25 装下 -> 一页
    const std::string content = "甲，乙"; // 9 字节
    PaginationConfig full = baseCfg(25, 10);
    std::vector<PageInfo> pages = mustPaginate(content, full, "punct-full");
    CHECK_EQ(pages.size(), 2u);
    PaginationConfig half = baseCfg(25, 10);
    half.estimator.punctuationWidthEm = 0.5f;
    pages = mustPaginate(content, half, "punct-half");
    CHECK_EQ(pages.size(), 1u);
    if (pages.size() == 1)
        CHECK_EQ(pages[0].endOffset, 9u);
    // CJK/ASCII 系数也可配：每字 20px，行宽 25 -> 每行 1 字 -> 2 页
    PaginationConfig fat = baseCfg(25, 10);
    fat.estimator.cjkWidthEm = 2.0f;
    pages = mustPaginate("一二", fat, "cjk-fat");
    CHECK_EQ(pages.size(), 2u);
}

TEST(pagination_estimator_model)
{
    EstimatingMeasurer m(10.0f); // 默认：ASCII 5px / CJK 10px / 标点 10px
    CHECK(std::fabs(m.codepointWidth('A') - 5.0) < kTol);
    CHECK(std::fabs(m.codepointWidth('0') - 5.0) < kTol);
    CHECK(std::fabs(m.codepointWidth(0x4E00) - 10.0) < kTol); // 一
    CHECK(std::fabs(m.codepointWidth(U'，') - 10.0) < kTol);
    CHECK(std::fabs(m.codepointWidth(U'。') - 10.0) < kTol);
    CHECK(std::fabs(m.codepointWidth(U'—') - 10.0) < kTol);
    CHECK(std::fabs(m.codepointWidth(U'…') - 10.0) < kTol);
    CHECK(std::fabs(m.measure("abc", 3) - 15.0) < kTol);
    CHECK(std::fabs(m.measure("甲a", 4) - 15.0) < kTol); // 3+1 字节

    EstimatorConfig est;
    est.punctuationWidthEm = 0.5f;
    EstimatingMeasurer m2(10.0f, est);
    CHECK(std::fabs(m2.codepointWidth(U'，') - 5.0) < kTol);
    CHECK(std::fabs(m2.codepointWidth(0x4E00) - 10.0) < kTol);
}

// 自定义测量器注入（未来真机字体测量走同一条路）
namespace {
class FixedMeasurer : public TextMeasurer {
public:
    float codepointWidth(uint32_t) const override { return 1.0f; }
};
} // namespace

TEST(pagination_custom_measurer)
{
    // 全部字符等宽 1px：行宽 4 -> 4 字/行，行高 1 -> 每页 1 行
    const std::string content = "甲乙丙丁戊己"; // 6 字，18 字节
    PaginationConfig cfg = baseCfg(4, 1);
    cfg.fontSize = 1;
    FixedMeasurer fm;
    Result<std::vector<PageInfo>> res =
        PaginationEngine::paginate(content, cfg, fm);
    CHECK(res.ok());
    if (res.ok()) {
        const std::vector<PageInfo>& pages = res.value();
        checkTiling(content, pages, "custom-measurer");
        CHECK_EQ(pages.size(), 2u);
        if (pages.size() == 2) {
            CHECK_EQ(pages[0].endOffset, 12u);  // "甲乙丙丁"
            CHECK_EQ(pages[1].startOffset, 12u); // "戊己"
        }
    }
    // 注入测量器时 estimator 参数不参与（也不因非法 estimator 报错）
    PaginationConfig cfg2 = cfg;
    cfg2.estimator.asciiWidthEm = -1;
    CHECK(PaginationEngine::paginate(content, cfg2, fm).ok());
}

// ---- 分类与换行性质表 ----

TEST(pagination_classify_and_traits)
{
    CHECK(classifyCodepoint('A') == CharClass::Half);
    CHECK(classifyCodepoint('0') == CharClass::Half);
    CHECK(classifyCodepoint(' ') == CharClass::Half);
    CHECK(classifyCodepoint(0x4E00) == CharClass::Full);  // 一
    CHECK(classifyCodepoint(0x3042) == CharClass::Full);  // あ
    CHECK(classifyCodepoint(0x30A2) == CharClass::Full);  // ア
    CHECK(classifyCodepoint(0xD55C) == CharClass::Full);  // 한
    CHECK(classifyCodepoint(0xFF11) == CharClass::Full);  // 全角 1
    CHECK(classifyCodepoint(0x3400) == CharClass::Full);  // 扩展 A
    CHECK(classifyCodepoint(0x1F600) == CharClass::Full); // emoji 按全宽估
    CHECK(classifyCodepoint(U'，') == CharClass::Punct);
    CHECK(classifyCodepoint(U'。') == CharClass::Punct);
    CHECK(classifyCodepoint(U'！') == CharClass::Punct);
    CHECK(classifyCodepoint(U'「') == CharClass::Punct);
    CHECK(classifyCodepoint(U'—') == CharClass::Punct);
    CHECK(classifyCodepoint(U'…') == CharClass::Punct);
    CHECK(classifyCodepoint(0x3000) == CharClass::Punct); // 全角空格
    CHECK(classifyCodepoint(0xFF65) == CharClass::Punct); // 半角 ・

    CharTraits t = codepointTraits(U'。');
    CHECK(!t.canStartLine); // 句号不落行首
    CHECK(t.canEndLine);
    t = codepointTraits(U'，');
    CHECK(!t.canStartLine);
    t = codepointTraits(U'！');
    CHECK(!t.canStartLine);
    t = codepointTraits(U'？');
    CHECK(!t.canStartLine);
    t = codepointTraits(U'；');
    CHECK(!t.canStartLine);
    t = codepointTraits(U'：');
    CHECK(!t.canStartLine);
    t = codepointTraits(U'」');
    CHECK(!t.canStartLine);
    t = codepointTraits(U'』');
    CHECK(!t.canStartLine);
    t = codepointTraits(U'「');
    CHECK(!t.canEndLine); // 引号不落行尾
    CHECK(t.canStartLine);
    t = codepointTraits(U'『');
    CHECK(!t.canEndLine);
    t = codepointTraits(U'（');
    CHECK(!t.canEndLine);
    t = codepointTraits(U'《');
    CHECK(!t.canEndLine);
    t = codepointTraits(U'”');
    CHECK(!t.canStartLine);
    t = codepointTraits('a');
    CHECK(t.isWordChar);
    CHECK(t.canStartLine);
    CHECK(t.canEndLine);
    t = codepointTraits(' ');
    CHECK(t.isSpace);
    t = codepointTraits('\n');
    CHECK(t.isHardBreak);
    t = codepointTraits('\r');
    CHECK(t.isHardBreak);
    t = codepointTraits(U'—');
    CHECK(t.isPairChar);
    t = codepointTraits(U'…');
    CHECK(t.isPairChar);
    t = codepointTraits(U'甲');
    CHECK(t.canStartLine);
    CHECK(t.canEndLine);
    CHECK(!t.isWordChar);
}

TEST(pagination_pair_chars_stay_glued)
{
    // 「……」成对连字中间不断：行宽只装 1 字时，"……" 也不拆成两行
    // （每字 10px，行宽 15 -> 只装 1 字；但 …… 之间无断点 -> 同行悬挂/强制）
    const std::string content = "……"; // 6 字节
    std::vector<PageInfo> pages = mustPaginate(content, baseCfg(15, 100), "pair");
    CHECK_EQ(pages.size(), 1u);
    if (pages.size() == 1)
        CHECK_EQ(pages[0].endOffset, 6u);
}

// ---- 大文本耗时探针（记录用；断言仅为防复杂度灾难，不是性能指标）----

TEST(pagination_large_text_probe)
{
    const std::string content = mixedText(300000, 2026);
    PaginationConfig cfg = baseCfg(400, 300);
    cfg.paragraphSpacing = 2;
    const auto t0 = std::chrono::steady_clock::now();
    Result<std::vector<PageInfo>> res = PaginationEngine::paginate(content, cfg);
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(res.ok());
    const double ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("    [探针] %zu 字节（中英混排）分页 %zu 页耗时 %.1f ms\n",
                content.size(), res.ok() ? res.value().size() : 0, ms);
    if (res.ok())
        checkTiling(content, res.value(), "large");
    CHECK(ms < 30000.0); // 仅防 O(n²) 灾难，非性能指标（host x86_64）
}
