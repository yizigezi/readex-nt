// test_reading_state.cpp — 阅读状态：进度 / 偏移↔页映射 / 书签 / 序列化
// （PR-5，计划书 §3.7/§七）
//
// 覆盖：进度保存/恢复、无效偏移/章节号错误码、close 后 InvalidHandle、
// 偏移↔页映射一致性（逐码点遍历）、文末光标、换分页配置后按 textOffset 恢复、
// 书签增删列表、序列化 round-trip（含中文/转义）、非法 JSON 不崩且原状态不变、
// 未知字段忽略、结构合法但越界的载荷（语义校验留给 setProgress）。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"
#include "test_support.hpp"

#include "readex/book.hpp"
#include "readex/pagination.hpp"
#include "readex/reading_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

using readex::Book;
using readex::Bookmark;
using readex::ErrorCode;
using readex::PageInfo;
using readex::PaginationConfig;
using readex::PaginationEngine;
using readex::ReadingState;
using readex::ReadingStateStore;
using readex::Result;
using readex::openBook;

namespace {

// 两章测试书（正文含中文/ASCII，关键词位置动态查）
const char* kRaw =
    "第一章\n"
    "你好世界。中文测试 abc。\n"
    "第二章\n"
    "第二章内容 xyz。\n";

// 打开共享测试书（每个用例自行 open，用完 close + 删文件的用例显式删）
Result<Book> openFixture()
{
    std::string path = testsup::writeTestFile("state_book.txt", kRaw);
    return openBook(path);
}

PaginationConfig cfg(float w)
{
    PaginationConfig c;
    c.width = w;
    c.height = 10; // 每页 1 行，保证小文本也有多页可查
    c.fontSize = 10;
    c.lineHeight = 1.0f;
    return c;
}

// 「文本里某码点边界偏移」构造：取 chapter 文本第 n 个字符的起始偏移
uint64_t codepointOffset(const std::string& text, size_t n)
{
    size_t i = 0, count = 0;
    while (i < text.size() && count < n) {
        ++i;
        while (i < text.size() &&
               (static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u)
            ++i;
        ++count;
    }
    return static_cast<uint64_t>(i);
}

} // namespace

TEST(state_default_progress)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;
    const ReadingState& p = book.value().state().progress();
    CHECK_EQ(p.chapterIndex, (size_t)0);
    CHECK_EQ(p.pageIndex, (size_t)0);
    CHECK_EQ(p.textOffset, (uint64_t)0);
    CHECK(book.value().state().bookmarks().empty());
}

TEST(state_set_get_progress)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;
    ReadingState st;
    st.chapterIndex = 1;
    st.pageIndex = 3;
    st.textOffset = 10; // 「第二章\n」= 10 字节后第 2 行行首（章内解码文本偏移）
    Result<void> r = book.value().state().setProgress(book.value(), st);
    CHECK(r.ok());
    const ReadingState& p = book.value().state().progress();
    CHECK_EQ(p.chapterIndex, (size_t)1);
    CHECK_EQ(p.pageIndex, (size_t)3);
    CHECK_EQ(p.textOffset, (uint64_t)10);
}

TEST(state_progress_invalid)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;
    ReadingStateStore& store = book.value().state();

    // 章节号越界 -> InvalidArgument
    ReadingState st;
    st.chapterIndex = 99;
    Result<void> r = store.setProgress(book.value(), st);
    CHECK(!r.ok());
    CHECK_EQ((int)r.error().code(), (int)ErrorCode::InvalidArgument);

    // 偏移越出章文本 -> InvalidArgument
    Result<std::string> ch1 = book.value().getChapter(1);
    CHECK(ch1.ok());
    if (!ch1.ok())
        return;
    st.chapterIndex = 1;
    st.textOffset = ch1.value().size() + 1;
    r = store.setProgress(book.value(), st);
    CHECK(!r.ok());
    CHECK_EQ((int)r.error().code(), (int)ErrorCode::InvalidArgument);

    // 偏移落在多字节字符中间 -> InvalidArgument（「你」= 3 字节，偏移 1）
    Result<std::string> ch0 = book.value().getChapter(0);
    CHECK(ch0.ok());
    if (!ch0.ok() || ch0.value().size() < 9)
        return;
    st.chapterIndex = 0;
    st.textOffset = 1;
    r = store.setProgress(book.value(), st);
    CHECK(!r.ok());
    CHECK_EQ((int)r.error().code(), (int)ErrorCode::InvalidArgument);

    // 文末位置（== 章文本长）合法
    st.textOffset = ch0.value().size();
    r = store.setProgress(book.value(), st);
    CHECK(r.ok());

    // close 后 -> InvalidHandle
    book.value().close();
    r = store.setProgress(book.value(), st);
    CHECK(!r.ok());
    CHECK_EQ((int)r.error().code(), (int)ErrorCode::InvalidHandle);
}

TEST(state_offset_page_mapping)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;
    Result<std::string> ch = book.value().getChapter(0);
    CHECK(ch.ok());
    if (!ch.ok())
        return;
    const std::string& text = ch.value();

    Result<std::vector<PageInfo>> pagesRes =
        PaginationEngine::paginate(text, cfg(60));
    CHECK(pagesRes.ok());
    if (!pagesRes.ok() || pagesRes.value().size() < 2)
        return;
    const std::vector<PageInfo>& pages = pagesRes.value();

    // 逐码点边界遍历：反查页必须包含该偏移（或文末贴末页）；正查 <= 偏移
    for (uint64_t off = 0; off <= text.size();) {
        Result<size_t> idx = readex::pageIndexForOffset(pages, off);
        CHECK(idx.ok());
        if (!idx.ok())
            break;
        const PageInfo& pg = pages[idx.value()];
        bool inside = (off >= pg.startOffset && off < pg.endOffset) ||
                      (off == text.size() && idx.value() == pages.size() - 1);
        if (!inside)
            ::microtest::reportFail(__FILE__, __LINE__,
                                    "反查页不包含偏移 " + std::to_string(off));
        Result<uint64_t> start = readex::offsetForPage(pages, idx.value());
        CHECK(start.ok());
        if (start.ok() && start.value() > off)
            ::microtest::reportFail(__FILE__, __LINE__,
                                    "正查页起点越过偏移");
        // 推进到下一个码点边界
        {
            uint64_t next = off + 1;
            while (next < text.size() &&
                   (static_cast<unsigned char>(text[static_cast<size_t>(next)]) &
                    0xC0u) == 0x80u)
                ++next;
            off = next;
        }
    }

    // 文末光标（== 末页终点）归末页
    Result<size_t> lastIdx =
        readex::pageIndexForOffset(pages, text.size());
    CHECK(lastIdx.ok());
    if (lastIdx.ok())
        CHECK_EQ(lastIdx.value(), pages.size() - 1);

    // 越界 / 空页表 -> InvalidArgument
    CHECK(!readex::pageIndexForOffset(pages, text.size() + 100).ok());
    CHECK(!readex::pageIndexForOffset({}, 0).ok());
    CHECK(!readex::offsetForPage(pages, pages.size()).ok());
}

TEST(state_locate_page_reconfig)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;
    Result<std::string> ch = book.value().getChapter(0);
    CHECK(ch.ok());
    if (!ch.ok())
        return;
    const std::string& text = ch.value();
    uint64_t off = codepointOffset(text, 4); // 第 5 个字符起点

    ReadingState st;
    st.chapterIndex = 0;
    st.textOffset = off;
    CHECK(book.value().state().setProgress(book.value(), st).ok());

    // 配置 A（窄）：定位页
    Result<std::vector<PageInfo>> pa =
        PaginationEngine::paginate(text, cfg(30));
    CHECK(pa.ok());
    if (!pa.ok())
        return;
    CHECK(book.value().state().locatePage(pa.value()).ok());
    size_t pageA = book.value().state().progress().pageIndex;
    CHECK(pageA < pa.value().size());

    // 换配置 B（更宽 -> 更少页）：按 textOffset 尽量恢复（映射到新页）
    Result<std::vector<PageInfo>> pb =
        PaginationEngine::paginate(text, cfg(300));
    CHECK(pb.ok());
    if (!pb.ok())
        return;
    Result<size_t> newPage = book.value().state().locatePage(pb.value());
    CHECK(newPage.ok());
    if (!newPage.ok())
        return;

    const ReadingState& p = book.value().state().progress();
    CHECK_EQ(p.textOffset, off); // 事实源不动
    CHECK_EQ(p.pageIndex, newPage.value());
    // 新页确实包含该偏移（或文末贴末页）
    CHECK(newPage.value() < pb.value().size());
    if (newPage.value() < pb.value().size()) {
        const PageInfo& pg = pb.value()[newPage.value()];
        CHECK(off >= pg.startOffset && off < pg.endOffset);
    }
    (void)pageA;

    // locatePage 对空页表报错且不动状态
    size_t before = book.value().state().progress().pageIndex;
    CHECK(!book.value().state().locatePage({}).ok());
    CHECK_EQ(book.value().state().progress().pageIndex, before);
}

TEST(state_bookmarks_crud)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;
    ReadingStateStore& store = book.value().state();

    Bookmark b0;
    b0.chapterIndex = 0;
    b0.textOffset = 0;
    b0.note = "开头";
    b0.createdAt = "2026-11-13T01:00:00+08:00";
    Bookmark b1;
    b1.chapterIndex = 0;
    b1.textOffset = 9;
    b1.note = "带\"引号\"和\\反斜杠\n换行";
    Bookmark b2;
    b2.chapterIndex = 1;
    b2.textOffset = 0;

    CHECK_EQ(store.addBookmark(book.value(), b0).value(), (size_t)0);
    CHECK_EQ(store.addBookmark(book.value(), b1).value(), (size_t)1);
    CHECK_EQ(store.addBookmark(book.value(), b2).value(), (size_t)2);
    CHECK_EQ(store.bookmarks().size(), (size_t)3);

    // 无效位置 -> InvalidArgument 且不入库
    Bookmark bad;
    bad.chapterIndex = 5;
    bad.textOffset = 0;
    Result<size_t> rb = store.addBookmark(book.value(), bad);
    CHECK(!rb.ok());
    CHECK_EQ((int)rb.error().code(), (int)ErrorCode::InvalidArgument);
    CHECK_EQ(store.bookmarks().size(), (size_t)3);

    // 删除中间一个：返回被删书签（字段完整），其余保持顺序
    Result<Bookmark> removed = store.removeBookmark(1);
    CHECK(removed.ok());
    if (removed.ok()) {
        CHECK_EQ(removed.value().note, b1.note);
        CHECK_EQ(removed.value().chapterIndex, (size_t)0);
        CHECK_EQ(removed.value().textOffset, (uint64_t)9);
        CHECK_EQ(removed.value().createdAt, std::string());
    }
    CHECK_EQ(store.bookmarks().size(), (size_t)2);
    if (store.bookmarks().size() == 2) {
        CHECK_EQ(store.bookmarks()[0].note, std::string("开头"));
        CHECK_EQ(store.bookmarks()[1].chapterIndex, (size_t)1);
    }

    // 下标越界 -> NotFound（「书签不存在」）
    Result<Bookmark> rr = store.removeBookmark(2);
    CHECK(!rr.ok());
    CHECK_EQ((int)rr.error().code(), (int)ErrorCode::NotFound);

    CHECK(store.removeBookmark(0).ok());
    CHECK(store.removeBookmark(0).ok());
    CHECK(store.bookmarks().empty());
}

TEST(state_serialize_roundtrip)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;
    ReadingStateStore a;
    ReadingState st;
    st.chapterIndex = 1;
    st.pageIndex = 2;
    st.textOffset = 10;
    CHECK(a.setProgress(book.value(), st).ok());

    Bookmark b;
    b.chapterIndex = 0;
    b.textOffset = 3;
    b.note = "引\"号\\反斜杠\n换行\t制表 中文✅";
    b.createdAt = "2026-11-13T01:40:00+08:00";
    CHECK(a.addBookmark(book.value(), b).ok());
    Bookmark b2;
    b2.chapterIndex = 1;
    b2.textOffset = 0;
    CHECK(a.addBookmark(book.value(), b2).ok());

    std::string json = a.serialize();
    ReadingStateStore c;
    Result<void> r = c.deserialize(json);
    CHECK(r.ok());
    if (!r.ok())
        return;
    CHECK_EQ(c.progress().chapterIndex, a.progress().chapterIndex);
    CHECK_EQ(c.progress().pageIndex, a.progress().pageIndex);
    CHECK_EQ(c.progress().textOffset, a.progress().textOffset);
    CHECK_EQ(c.bookmarks().size(), a.bookmarks().size());
    if (c.bookmarks().size() == a.bookmarks().size()) {
        for (size_t i = 0; i < c.bookmarks().size(); ++i) {
            CHECK_EQ(c.bookmarks()[i].chapterIndex, a.bookmarks()[i].chapterIndex);
            CHECK_EQ(c.bookmarks()[i].textOffset, a.bookmarks()[i].textOffset);
            CHECK_EQ(c.bookmarks()[i].note, a.bookmarks()[i].note);
            CHECK_EQ(c.bookmarks()[i].createdAt, a.bookmarks()[i].createdAt);
        }
    }
    // 稳定：再序列化 == 原串
    CHECK_EQ(c.serialize(), json);

    // 空状态 round-trip
    ReadingStateStore empty;
    ReadingStateStore empty2;
    CHECK(empty2.deserialize(empty.serialize()).ok());
    CHECK_EQ(empty2.serialize(), empty.serialize());
}

TEST(state_deserialize_invalid)
{
    Result<Book> book = openFixture();
    CHECK(book.ok());
    if (!book.ok())
        return;

    const char* bad[] = {
        "",                                    // 空串
        "{",                                   // 截断
        "[]",                                  // 顶层不是对象
        "{}",                                  // 缺 version
        "garbage",                             // 非 JSON
        "{\"version\":2}",                     // 版本不认识
        "{\"version\":\"1\"}",                 // version 类型错
        "{\"version\":1,}",                    // 尾随逗号
        "{\"version\":1}x",                    // 对象后多余内容
        "{\"version\":1,\"progress\":5}",      // progress 类型错
        "{\"version\":1,\"progress\":{\"textOffset\":-1}}",   // 负数
        "{\"version\":1,\"progress\":{\"textOffset\":1.5}}", // 小数
        "{\"version\":1,\"progress\":{\"textOffset\":\"6\"}}", // 字符串当数
        "{\"version\":1,\"progress\":{\"textOffset\":99999999999999999999999}}", // 溢出
        "{\"version\":1,\"progress\":{\"textOffset\":00}}",  // 前导零
        "{\"version\":1,\"bookmarks\":5}",     // bookmarks 类型错
        "{\"version\":1,\"bookmarks\":[5]}",   // 条目不是对象
        "{\"version\":1,\"bookmarks\":[{\"note\":\"\\q\"}]}", // 坏转义
        "{\"version\":1,\"bookmarks\":[{\"note\":\"\\u12\"}]}", // \u 截断
        "{\"version\":1,\"bookmarks\":[{\"note\":\"a\nb\"}]}", // 未转义控制符
        "{\"version\":1,\"bookmarks\":[{\"note\":\"\\ud800\"}]}", // 孤立高位代理
    };

    // 先放一个已知状态，验证失败加载不破坏原状态
    ReadingState st;
    st.chapterIndex = 1;
    st.textOffset = 10;
    CHECK(book.value().state().setProgress(book.value(), st).ok());

    for (const char* s : bad) {
        Result<void> r = book.value().state().deserialize(s);
        if (r.ok())
            ::microtest::reportFail(__FILE__, __LINE__,
                                    std::string("应拒绝非法 JSON: ") + s);
        else if (r.error().code() != ErrorCode::ParseError)
            ::microtest::reportFail(__FILE__, __LINE__,
                                    std::string("错误码应为 ParseError: ") + s);
    }
    // 全部失败后原状态不变
    CHECK_EQ(book.value().state().progress().chapterIndex, (size_t)1);
    CHECK_EQ(book.value().state().progress().textOffset, (uint64_t)10);
}

TEST(state_deserialize_lenient)
{
    // 未知字段忽略（向前兼容）；缺省字段取默认值；结构合法但语义越界可载入
    //（语义校验在 setProgress/addBookmark，见 reading_state.hpp 头注释）
    Result<void> r = ReadingStateStore().deserialize(
        "{\"version\":1,\"future\":{\"x\":[1,2,{\"y\":null}]},"
        "\"progress\":{\"chapterIndex\":99,\"textOffset\":5,\"zzz\":\"?\"},"
        "\"bookmarks\":[{\"chapterIndex\":1,\"note\":\"好\",\"unknown\":true}],"
        "\"tail\":false}");
    CHECK(r.ok());

    ReadingStateStore st;
    CHECK(st.deserialize("{\"version\":1,\"progress\":{\"textOffset\":7}}").ok());
    CHECK_EQ(st.progress().textOffset, (uint64_t)7);
    CHECK_EQ(st.progress().chapterIndex, (size_t)0); // 缺省 0
    CHECK_EQ(st.progress().pageIndex, (size_t)0);

    // \uXXXX 转义还原（含代理对）
    ReadingStateStore esc;
    CHECK(esc.deserialize(
              "{\"version\":1,\"bookmarks\":[{\"note\":\"\\u4e2d\\u6587\\ud83d\\ude00\"}]}")
              .ok());
    if (!esc.bookmarks().empty())
        CHECK_EQ(esc.bookmarks()[0].note, std::string("中文\U0001F600"));
}
