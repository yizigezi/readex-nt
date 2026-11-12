// reading_state.cpp — 阅读状态实现（PR-5，计划书 §3.7）
//
// 进度校验 / 页区间反查 / 书签 / JSON 序列化（version 1）。语义、格式与偏差
// 说明见 reading_state.hpp 头注释。不依赖 QuickJS。
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/reading_state.hpp"

#include "readex/book.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace readex {

// ---- 语义校验：章节号 + 章内偏移（setProgress / addBookmark / 绑定层加载复核共用）----
Result<void> validateStateLocation(const Book& book, size_t chapterIndex,
                                   uint64_t textOffset)
{
    Result<std::vector<ChapterInfo>> chapters = book.getChapters();
    if (!chapters.ok())
        return Result<void>::failure(chapters.error()); // InvalidHandle 等透传
    if (chapterIndex >= chapters.value().size())
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "章节索引 " + std::to_string(chapterIndex) + " 越界（共 " +
                std::to_string(chapters.value().size()) + " 章）");

    Result<std::string> text = book.getChapter(chapterIndex);
    if (!text.ok())
        return Result<void>::failure(text.error());
    const std::string& s = text.value();
    if (textOffset > s.size())
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "textOffset " + std::to_string(textOffset) +
                " 越出章内文本（长 " + std::to_string(s.size()) + " 字节）");
    if (textOffset < s.size() &&
        (static_cast<unsigned char>(s[static_cast<size_t>(textOffset)]) &
         0xC0u) == 0x80u)
        return Result<void>::failure(
            ErrorCode::InvalidArgument,
            "textOffset " + std::to_string(textOffset) +
                " 落在多字节字符中间（不在码点边界）");
    return Result<void>::success();
}

namespace {

// ---- JSON：序列化 ----
void appendJsonEscaped(std::string& out, const std::string& v)
{
    out += '"';
    for (size_t i = 0; i < v.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(v[i]);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c); // UTF-8 原样保留
            }
        }
    }
    out += '"';
}

void appendBookmarkJson(std::string& out, const Bookmark& bm)
{
    out += "{\"chapterIndex\":" + std::to_string(bm.chapterIndex);
    out += ",\"textOffset\":" + std::to_string(bm.textOffset);
    out += ",\"note\":";
    appendJsonEscaped(out, bm.note);
    out += ",\"createdAt\":";
    appendJsonEscaped(out, bm.createdAt);
    out += '}';
}

// ---- JSON：解析（最小严格解析器，只服务本 schema，未知字段跳过）----
struct JsonParser {
    const std::string& s;
    size_t pos = 0;
    std::string error;

    explicit JsonParser(const std::string& str) : s(str) {}

    bool fail(const std::string& msg)
    {
        if (error.empty())
            error = msg + "（位置 " + std::to_string(pos) + "）";
        return false;
    }
    bool ok() const { return error.empty(); }

    void skipWs()
    {
        while (pos < s.size() &&
               (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' ||
                s[pos] == '\r'))
            ++pos;
    }

    bool expect(char c)
    {
        skipWs();
        if (pos < s.size() && s[pos] == c) {
            ++pos;
            return true;
        }
        return fail(std::string("期望 '") + c + "'");
    }

    bool atEnd()
    {
        skipWs();
        return pos >= s.size();
    }

    // 严格字符串（含转义）；未转义控制字符/坏转义/截断 -> 失败
    bool parseString(std::string* out)
    {
        out->clear();
        if (!expect('"'))
            return false;
        while (pos < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[pos]);
            if (c == '"') {
                ++pos;
                return true;
            }
            if (c < 0x20)
                return fail("字符串含未转义控制字符");
            if (c != '\\') {
                *out += s[pos];
                ++pos;
                continue;
            }
            ++pos; // 跳过 '\'
            if (pos >= s.size())
                return fail("转义序列截断");
            char e = s[pos++];
            switch (e) {
            case '"': *out += '"'; break;
            case '\\': *out += '\\'; break;
            case '/': *out += '/'; break;
            case 'b': *out += '\b'; break;
            case 'f': *out += '\f'; break;
            case 'n': *out += '\n'; break;
            case 'r': *out += '\r'; break;
            case 't': *out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!parseHex4(&cp))
                    return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) { // 高位代理须配对
                    if (pos + 1 >= s.size() || s[pos] != '\\' ||
                        s[pos + 1] != 'u')
                        return fail("高位代理项缺少配对的 \\u 低位代理项");
                    pos += 2;
                    unsigned lo = 0;
                    if (!parseHex4(&lo))
                        return false;
                    if (lo < 0xDC00 || lo > 0xDFFF)
                        return fail("低位代理项超出范围");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("孤立的低位代理项");
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return fail("未知转义序列");
            }
        }
        return fail("字符串未闭合");
    }

    bool parseHex4(unsigned* out)
    {
        if (pos + 4 > s.size())
            return fail("\\u 转义截断");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s[pos + static_cast<size_t>(i)];
            unsigned d;
            if (c >= '0' && c <= '9')
                d = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                d = static_cast<unsigned>(c - 'a') + 10;
            else if (c >= 'A' && c <= 'F')
                d = static_cast<unsigned>(c - 'A') + 10;
            else
                return fail("\\u 转义含非十六进制字符");
            v = (v << 4) | d;
        }
        pos += 4;
        *out = v;
        return true;
    }

    static void appendUtf8(std::string* out, unsigned cp)
    {
        if (cp < 0x80) {
            *out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            *out += static_cast<char>(0xC0 | (cp >> 6));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            *out += static_cast<char>(0xE0 | (cp >> 12));
            *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            *out += static_cast<char>(0xF0 | (cp >> 18));
            *out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // 严格非负整数（十进制数字，无负号/小数/指数/前导零多位）；溢出 -> 失败
    bool parseUint64(uint64_t* out)
    {
        skipWs();
        if (pos >= s.size() || s[pos] < '0' || s[pos] > '9')
            return fail("期望非负整数");
        if (s[pos] == '0' && pos + 1 < s.size() &&
            s[pos + 1] >= '0' && s[pos + 1] <= '9')
            return fail("数字有多余前导零");
        uint64_t v = 0;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            uint64_t d = static_cast<uint64_t>(s[pos] - '0');
            if (v > (UINT64_MAX - d) / 10)
                return fail("整数超出 64 位范围");
            v = v * 10 + d;
            ++pos;
        }
        // 数字后面紧跟小数/指数/其它数字语义 -> 不是纯整数
        if (pos < s.size() &&
            (s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' ||
             s[pos] == '+' || s[pos] == '-'))
            return fail("数值不是非负整数");
        *out = v;
        return true;
    }

    // 跳过任意 JSON 值（未知字段用）；只做结构配平，不做语义校验
    bool skipValue()
    {
        skipWs();
        if (pos >= s.size())
            return fail("期望值");
        char c = s[pos];
        if (c == '"') {
            std::string tmp;
            return parseString(&tmp);
        }
        if (c == '{') {
            ++pos;
            skipWs();
            if (pos < s.size() && s[pos] == '}') {
                ++pos;
                return true;
            }
            for (;;) {
                std::string key;
                if (!parseString(&key))
                    return false;
                if (!expect(':'))
                    return false;
                if (!skipValue())
                    return false;
                skipWs();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                return expect('}');
            }
        }
        if (c == '[') {
            ++pos;
            skipWs();
            if (pos < s.size() && s[pos] == ']') {
                ++pos;
                return true;
            }
            for (;;) {
                if (!skipValue())
                    return false;
                skipWs();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                return expect(']');
            }
        }
        if (c == '-' || (c >= '0' && c <= '9')) { // 数字整体跳过（不要求整数）
            if (c == '-')
                ++pos;
            while (pos < s.size() &&
                   ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.' ||
                    s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' ||
                    s[pos] == '-'))
                ++pos;
            return true;
        }
        if (s.compare(pos, 4, "true") == 0) {
            pos += 4;
            return true;
        }
        if (s.compare(pos, 5, "false") == 0) {
            pos += 5;
            return true;
        }
        if (s.compare(pos, 4, "null") == 0) {
            pos += 4;
            return true;
        }
        return fail("无法识别的值");
    }
};

// 读 size_t 字段：uint64 + 平台范围检查（32 位设备上防截断）
bool readSizeField(JsonParser& p, const char* what, size_t* out)
{
    uint64_t v = 0;
    if (!p.parseUint64(&v))
        return false;
    if (v > static_cast<uint64_t>(static_cast<size_t>(-1)))
        return p.fail(std::string(what) + " 超出平台 size_t 范围");
    *out = static_cast<size_t>(v);
    return true;
}

// progress 对象：已消费 '{'，解析到 '}'（未知字段跳过）
bool parseProgressObject(JsonParser& p, ReadingState* st)
{
    p.skipWs();
    if (p.pos < p.s.size() && p.s[p.pos] == '}') {
        ++p.pos;
        return true;
    }
    for (;;) {
        std::string key;
        if (!p.parseString(&key))
            return false;
        if (!p.expect(':'))
            return false;
        if (key == "chapterIndex") {
            if (!readSizeField(p, "progress.chapterIndex", &st->chapterIndex))
                return false;
        } else if (key == "pageIndex") {
            if (!readSizeField(p, "progress.pageIndex", &st->pageIndex))
                return false;
        } else if (key == "textOffset") {
            if (!p.parseUint64(&st->textOffset))
                return false;
        } else {
            if (!p.skipValue())
                return false;
        }
        p.skipWs();
        if (p.pos < p.s.size() && p.s[p.pos] == ',') {
            ++p.pos;
            continue;
        }
        return p.expect('}');
    }
}

// bookmarks 条目对象：已消费 '{'，解析到 '}'（未知字段跳过）
bool parseBookmarkObject(JsonParser& p, Bookmark* bm)
{
    p.skipWs();
    if (p.pos < p.s.size() && p.s[p.pos] == '}') {
        ++p.pos;
        return true;
    }
    for (;;) {
        std::string key;
        if (!p.parseString(&key))
            return false;
        if (!p.expect(':'))
            return false;
        if (key == "chapterIndex") {
            if (!readSizeField(p, "bookmarks[].chapterIndex", &bm->chapterIndex))
                return false;
        } else if (key == "textOffset") {
            if (!p.parseUint64(&bm->textOffset))
                return false;
        } else if (key == "note") {
            if (!p.parseString(&bm->note))
                return false;
        } else if (key == "createdAt") {
            if (!p.parseString(&bm->createdAt))
                return false;
        } else {
            if (!p.skipValue())
                return false;
        }
        p.skipWs();
        if (p.pos < p.s.size() && p.s[p.pos] == ',') {
            ++p.pos;
            continue;
        }
        return p.expect('}');
    }
}

bool parseBookmarksArray(JsonParser& p, std::vector<Bookmark>* out)
{
    if (!p.expect('['))
        return false;
    p.skipWs();
    if (p.pos < p.s.size() && p.s[p.pos] == ']') {
        ++p.pos;
        return true;
    }
    for (;;) {
        if (!p.expect('{'))
            return false;
        Bookmark bm;
        if (!parseBookmarkObject(p, &bm))
            return false;
        out->push_back(std::move(bm));
        p.skipWs();
        if (p.pos < p.s.size() && p.s[p.pos] == ',') {
            ++p.pos;
            continue;
        }
        return p.expect(']');
    }
}

} // namespace

// ---- 纯映射函数 ----
Result<size_t> pageIndexForOffset(const std::vector<PageInfo>& pages,
                                  uint64_t textOffset)
{
    if (pages.empty())
        return Result<size_t>::failure(ErrorCode::InvalidArgument,
                                       "pageIndexForOffset: 页表为空");
    for (size_t i = 0; i < pages.size(); ++i) {
        if (textOffset >= pages[i].startOffset &&
            textOffset < pages[i].endOffset)
            return Result<size_t>::success(i);
    }
    // 文末光标（== 末页终点）归末页
    if (textOffset == pages.back().endOffset)
        return Result<size_t>::success(pages.size() - 1);
    return Result<size_t>::failure(
        ErrorCode::InvalidArgument,
        "pageIndexForOffset: 偏移 " + std::to_string(textOffset) +
            " 不在任何页区间内");
}

Result<uint64_t> offsetForPage(const std::vector<PageInfo>& pages,
                               size_t pageIndex)
{
    if (pageIndex >= pages.size())
        return Result<uint64_t>::failure(
            ErrorCode::InvalidArgument,
            "offsetForPage: 页号 " + std::to_string(pageIndex) + " 越界（共 " +
                std::to_string(pages.size()) + " 页）");
    return Result<uint64_t>::success(pages[pageIndex].startOffset);
}

// ---- 进度 ----
Result<void> ReadingStateStore::setProgress(const Book& book,
                                            const ReadingState& state)
{
    Result<void> v = validateStateLocation(book, state.chapterIndex, state.textOffset);
    if (!v.ok())
        return v;
    progress_ = state;
    return Result<void>::success();
}

Result<size_t> ReadingStateStore::locatePage(const std::vector<PageInfo>& pages)
{
    Result<size_t> r = pageIndexForOffset(pages, progress_.textOffset);
    if (!r.ok())
        return r;
    progress_.pageIndex = r.value();
    return r;
}

// ---- 书签 ----
Result<size_t> ReadingStateStore::addBookmark(const Book& book,
                                              const Bookmark& bookmark)
{
    Result<void> v =
        validateStateLocation(book, bookmark.chapterIndex, bookmark.textOffset);
    if (!v.ok())
        return Result<size_t>::failure(v.error());
    bookmarks_.push_back(bookmark);
    return Result<size_t>::success(bookmarks_.size() - 1);
}

Result<Bookmark> ReadingStateStore::removeBookmark(size_t index)
{
    if (index >= bookmarks_.size())
        return Result<Bookmark>::failure(
            ErrorCode::NotFound,
            "书签下标 " + std::to_string(index) + " 不存在（共 " +
                std::to_string(bookmarks_.size()) + " 个）");
    Bookmark removed = bookmarks_[index];
    bookmarks_.erase(bookmarks_.begin() + static_cast<ptrdiff_t>(index));
    return Result<Bookmark>::success(std::move(removed));
}

// ---- 序列化 ----
std::string ReadingStateStore::serialize() const
{
    std::string out;
    out += "{\"version\":1,\"progress\":{";
    out += "\"chapterIndex\":" + std::to_string(progress_.chapterIndex);
    out += ",\"pageIndex\":" + std::to_string(progress_.pageIndex);
    out += ",\"textOffset\":" + std::to_string(progress_.textOffset);
    out += "},\"bookmarks\":[";
    for (size_t i = 0; i < bookmarks_.size(); ++i) {
        if (i > 0)
            out += ',';
        appendBookmarkJson(out, bookmarks_[i]);
    }
    out += "]}";
    return out;
}

Result<void> ReadingStateStore::deserialize(const std::string& json)
{
    JsonParser p(json);
    if (!p.expect('{'))
        return Result<void>::failure(ErrorCode::ParseError,
                                     "deserialize: " + p.error);

    ReadingState st; // 解析到临时变量，成功才整体替换（失败保持原样）
    std::vector<Bookmark> bms;
    bool sawVersion = false;

    p.skipWs();
    if (p.pos < p.s.size() && p.s[p.pos] == '}') {
        ++p.pos;
    } else {
        for (;;) {
            std::string key;
            if (!p.parseString(&key))
                return Result<void>::failure(ErrorCode::ParseError,
                                             "deserialize: " + p.error);
            if (!p.expect(':'))
                return Result<void>::failure(ErrorCode::ParseError,
                                             "deserialize: " + p.error);
            if (key == "version") {
                uint64_t v = 0;
                if (!p.parseUint64(&v))
                    return Result<void>::failure(ErrorCode::ParseError,
                                                 "deserialize: " + p.error);
                sawVersion = true;
                if (v != 1)
                    return Result<void>::failure(
                        ErrorCode::ParseError,
                        "deserialize: 不支持的序列化版本 " +
                            std::to_string(v) + "（当前只支持 1）");
            } else if (key == "progress") {
                if (!p.expect('{') || !parseProgressObject(p, &st))
                    return Result<void>::failure(ErrorCode::ParseError,
                                                 "deserialize: " + p.error);
            } else if (key == "bookmarks") {
                if (!parseBookmarksArray(p, &bms))
                    return Result<void>::failure(ErrorCode::ParseError,
                                                 "deserialize: " + p.error);
            } else {
                if (!p.skipValue()) // 未知字段忽略（向前兼容）
                    return Result<void>::failure(ErrorCode::ParseError,
                                                 "deserialize: " + p.error);
            }
            p.skipWs();
            if (p.pos < p.s.size() && p.s[p.pos] == ',') {
                ++p.pos;
                continue;
            }
            if (!p.expect('}'))
                return Result<void>::failure(ErrorCode::ParseError,
                                             "deserialize: " + p.error);
            break;
        }
    }

    if (!sawVersion)
        return Result<void>::failure(ErrorCode::ParseError,
                                     "deserialize: 缺少 version 字段");
    if (!p.atEnd())
        return Result<void>::failure(ErrorCode::ParseError,
                                     "deserialize: 对象后有多余内容");

    progress_ = st;
    bookmarks_ = std::move(bms);
    return Result<void>::success();
}

} // namespace readex
