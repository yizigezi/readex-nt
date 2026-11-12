// test_search.cpp — 全文搜索（PR-5，计划书 §3.6/§七）
//
// 覆盖：中/英关键词、多匹配、limit 限流、无匹配、非重叠语义、context 边界
// （首尾截断 / 码点安全 / 长度可配）、空关键词与 limit 非法、整书多章搜索
// （章节归属、跨章 limit、close 后 InvalidHandle）、大文本耗时探针（记录实数）。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"
#include "test_support.hpp"

#include "readex/book.hpp"
#include "readex/search.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using readex::Book;
using readex::ErrorCode;
using readex::Result;
using readex::SearchEngine;
using readex::SearchOptions;
using readex::SearchResult;
using readex::openBook;

namespace {

// 断言结果里的每个 context 都是原文子串且含关键词、偏移精确指向关键词
void checkResults(const std::string& text, const std::string& keyword,
                  const std::vector<SearchResult>& rs, const char* what)
{
    for (const SearchResult& r : rs) {
        size_t p = static_cast<size_t>(r.offset);
        if (p + keyword.size() > text.size() ||
            text.compare(p, keyword.size(), keyword) != 0)
            ::microtest::reportFail(__FILE__, __LINE__,
                                    std::string(what) + ": offset 未指向关键词");
        if (r.context.find(keyword) == std::string::npos)
            ::microtest::reportFail(__FILE__, __LINE__,
                                    std::string(what) + ": context 不含关键词");
        if (text.find(r.context) == std::string::npos)
            ::microtest::reportFail(__FILE__, __LINE__,
                                    std::string(what) + ": context 不是原文子串");
    }
}

std::vector<SearchResult> mustSearchText(const std::string& text,
                                         const std::string& keyword,
                                         size_t limit,
                                         const char* what,
                                         const SearchOptions& opts = SearchOptions())
{
    Result<std::vector<SearchResult>> res =
        SearchEngine::searchText(text, keyword, limit, opts);
    if (!res.ok()) {
        ::microtest::reportFail(__FILE__, __LINE__,
                                std::string(what) + ": 不应失败: " +
                                    res.error().toString());
        return {};
    }
    checkResults(text, keyword, res.value(), what);
    return res.value();
}

} // namespace

TEST(search_cn_basic)
{
    // 3 个「今天」：偏移 0 / 21 / 39（每字 3 字节）
    std::string text = "今天天气很好。今天下雨了。今天晴天。";
    std::vector<SearchResult> rs = mustSearchText(text, "今天", 10, "cn");
    CHECK_EQ(rs.size(), (size_t)3);
    if (rs.size() == 3) {
        CHECK_EQ(rs[0].offset, (uint64_t)0);
        CHECK_EQ(rs[1].offset, (uint64_t)21);
        CHECK_EQ(rs[2].offset, (uint64_t)39);
        CHECK_EQ(rs[0].chapterIndex, (size_t)0); // 单章缺省 0
        CHECK(rs[0].context.find("今天天气") != std::string::npos);
    }
}

TEST(search_en_basic)
{
    std::string text = "hello world, hello readex, hello";
    std::vector<SearchResult> rs = mustSearchText(text, "hello", 10, "en");
    CHECK_EQ(rs.size(), (size_t)3);
    if (rs.size() == 3) {
        CHECK_EQ(rs[0].offset, (uint64_t)0);
        CHECK_EQ(rs[1].offset, (uint64_t)13);
        CHECK_EQ(rs[2].offset, (uint64_t)27);
    }
}

TEST(search_limit)
{
    std::string text = "中中中中中"; // 5 个匹配，各 3 字节
    CHECK_EQ(mustSearchText(text, "中", 2, "limit2").size(), (size_t)2);
    CHECK_EQ(mustSearchText(text, "中", 5, "limit5").size(), (size_t)5);
    CHECK_EQ(mustSearchText(text, "中", 100, "limit100").size(), (size_t)5);
    // 命中即停：limit 截断结果按出现顺序（偏移递增）
    std::vector<SearchResult> rs = mustSearchText(text, "中", 3, "order");
    if (rs.size() == 3) {
        CHECK(rs[0].offset < rs[1].offset && rs[1].offset < rs[2].offset);
    }
}

TEST(search_no_match)
{
    std::vector<SearchResult> rs =
        mustSearchText("abcdefg", "xyz", 10, "no-match");
    CHECK(rs.empty());
    // 关键词比文本长
    CHECK(mustSearchText("ab", "abcdef", 10, "kw-longer").empty());
    // 空文本
    CHECK(mustSearchText("", "x", 10, "empty-text").empty());
}

TEST(search_invalid_args)
{
    Result<std::vector<SearchResult>> r1 =
        SearchEngine::searchText("abc", "", 10);
    CHECK(!r1.ok());
    CHECK_EQ((int)r1.error().code(), (int)ErrorCode::InvalidArgument);

    Result<std::vector<SearchResult>> r2 =
        SearchEngine::searchText("abc", "a", 0);
    CHECK(!r2.ok());
    CHECK_EQ((int)r2.error().code(), (int)ErrorCode::InvalidArgument);
}

TEST(search_non_overlap)
{
    // 「aa」在「aaaa」里互不重叠：偏移 0 / 2（不是 0/1/2）
    std::vector<SearchResult> rs = mustSearchText("aaaa", "aa", 10, "overlap");
    CHECK_EQ(rs.size(), (size_t)2);
    if (rs.size() == 2) {
        CHECK_EQ(rs[0].offset, (uint64_t)0);
        CHECK_EQ(rs[1].offset, (uint64_t)2);
    }
}

TEST(search_context_bounds)
{
    // 匹配在文本开头：context 前侧自然截断到 0
    std::string text = "开头匹配后面的长文本还有很多字";
    std::vector<SearchResult> rs = mustSearchText(text, "开头", 10, "at-start");
    if (rs.size() == 1)
        CHECK_EQ(rs[0].context.substr(0, 12), std::string("开头匹配")); // 4 字 × 3 字节

    // 匹配在文本结尾：context 后侧自然截断到文末
    rs = mustSearchText(text, "多字", 10, "at-end");
    if (rs.size() == 1)
        CHECK_EQ(rs[0].context.substr(rs[0].context.size() - 9),
                 std::string("很多字")); // 3 字 × 3 字节

    // 码点安全：contextBefore=2 不足一个汉字（3 字节），起点吸附回字符首
    // 「甲乙丙丁戊」中「戊」在偏移 12；12-2=10 落在「丁」中间 -> 退到 9
    std::string cn = "甲乙丙丁戊";
    SearchOptions opts;
    opts.contextBefore = 2;
    opts.contextAfter = 0;
    rs = mustSearchText(cn, "戊", 10, "snap-back", opts);
    CHECK_EQ(rs.size(), (size_t)1);
    if (rs.size() == 1)
        CHECK_EQ(rs[0].context, std::string("丁戊"));

    // contextAfter=1 不足一个汉字：终点吸附到字符尾（含「戊」后无字 -> 文末）
    opts.contextBefore = 0;
    opts.contextAfter = 1;
    rs = mustSearchText("戊己庚", "戊", 10, "snap-fwd", opts);
    CHECK_EQ(rs.size(), (size_t)1);
    if (rs.size() == 1)
        CHECK_EQ(rs[0].context, std::string("戊己")); // 己 3 字节，1 吸附成整字

    // 中点匹配两侧吸附：context 恒为合法 UTF-8（首字节非续字节由精确断言保证）
    opts.contextBefore = 4;
    opts.contextAfter = 4;
    rs = mustSearchText("一二三四五六七八", "五", 10, "mid-snap", opts);
    if (rs.size() == 1) {
        CHECK(rs[0].context.find("五") != std::string::npos);
        CHECK(rs[0].context.size() <= 6 + 3 + 6 + 6); // 有界即可
    }
}

TEST(search_context_config)
{
    std::string text = "abcdefg";
    // 双 0：context == 关键词本身
    SearchOptions opts;
    opts.contextBefore = 0;
    opts.contextAfter = 0;
    std::vector<SearchResult> rs = mustSearchText(text, "cd", 10, "ctx0", opts);
    if (rs.size() == 1)
        CHECK_EQ(rs[0].context, std::string("cd"));

    // 超长预算：context == 全文
    opts.contextBefore = 10000;
    opts.contextAfter = 10000;
    rs = mustSearchText(text, "cd", 10, "ctx-full", opts);
    if (rs.size() == 1)
        CHECK_EQ(rs[0].context, text);

    // 默认预算 32/32：短文本全覆盖
    rs = mustSearchText(text, "cd", 10, "ctx-default");
    if (rs.size() == 1)
        CHECK_EQ(rs[0].context, text);
}

TEST(search_book_multi_chapter)
{
    std::string raw =
        "第一章 开始\n"
        "甲乙丙关键词一\n"
        "第二章 中间\n"
        "关键词二关键词三\n"
        "第三章 结束\n"
        "无匹配内容\n";
    std::string path = testsup::writeTestFile("search_book.txt", raw);
    Result<Book> book = openBook(path);
    CHECK(book.ok());
    if (!book.ok()) {
        testsup::removeTestFile(path);
        return;
    }

    Result<std::vector<SearchResult>> res =
        SearchEngine::search(book.value(), "关键词", 10);
    CHECK(res.ok());
    if (res.ok()) {
        const std::vector<SearchResult>& rs = res.value();
        CHECK_EQ(rs.size(), (size_t)3);
        if (rs.size() == 3) {
            CHECK_EQ(rs[0].chapterIndex, (size_t)0);
            CHECK_EQ(rs[1].chapterIndex, (size_t)1);
            CHECK_EQ(rs[2].chapterIndex, (size_t)1);
            // 偏移精确指向该章解码文本里的关键词（章内偏移，含标题行）
            for (const SearchResult& r : rs) {
                Result<std::string> ch = book.value().getChapter(r.chapterIndex);
                CHECK(ch.ok());
                if (ch.ok()) {
                    size_t p = static_cast<size_t>(r.offset);
                    CHECK(p + 9 <= ch.value().size());
                    CHECK_EQ(ch.value().compare(p, 9, "关键词"), 0);
                }
            }
        }
    }

    // 跨章 limit：命中即停（前 2 个）
    res = SearchEngine::search(book.value(), "关键词", 2);
    CHECK(res.ok());
    if (res.ok()) {
        CHECK_EQ(res.value().size(), (size_t)2);
        if (res.value().size() == 2)
            CHECK_EQ(res.value()[1].chapterIndex, (size_t)1);
    }

    // 无匹配 -> 空
    res = SearchEngine::search(book.value(), "不存在的词", 10);
    CHECK(res.ok());
    if (res.ok())
        CHECK(res.value().empty());

    // close 后搜索 -> InvalidHandle
    book.value().close();
    res = SearchEngine::search(book.value(), "关键词", 10);
    CHECK(!res.ok());
    CHECK_EQ((int)res.error().code(), (int)ErrorCode::InvalidHandle);

    testsup::removeTestFile(path);
}

TEST(search_big_text_probe)
{
    // 大文本耗时探针（记录实数，不设虚假指标）：
    // 约 1 MiB 中英混排，全量无匹配扫描 + 全量多匹配扫描，灾难上限 30s。
    std::string unit = "这是探针文本段落, containing ascii words and 中文. ";
    std::string text;
    while (text.size() < 1024 * 1024)
        text += unit;

    auto t0 = std::chrono::steady_clock::now();
    Result<std::vector<SearchResult>> r1 =
        SearchEngine::searchText(text, "绝不存在的关键词xyz", 10);
    auto t1 = std::chrono::steady_clock::now();
    Result<std::vector<SearchResult>> r2 =
        SearchEngine::searchText(text, "中文", 1000000);
    auto t2 = std::chrono::steady_clock::now();

    CHECK(r1.ok() && r1.value().empty());
    CHECK(r2.ok());
    double ms1 =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    double ms2 =
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::printf("    [探针] 1MiB 文本: 无匹配全量扫描 %.2f ms, 匹配 %zu 处全量 %.2f ms\n",
                ms1, r2.ok() ? r2.value().size() : 0, ms2);
    CHECK(ms1 < 30000.0); // 仅灾难上限（O(n²) 防线），非性能指标
    CHECK(ms2 < 30000.0);
}
