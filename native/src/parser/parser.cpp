// parser.cpp — TXT 章节解析实现（PR-3，计划书 §3.4）
//
// 组成（规则与解析逻辑分离，规则表只在 defaultChapterRules() 一处）：
//   1. UTF-8 行文本工具：码点遍历 / 前缀比较 / 空白裁剪
//   2. 标题匹配器 matchChapterTitle：纯函数，只消费规则表
//   3. 扫描器 scanChapters：字节层编码感知行切分（精确字节偏移）+
//      逐行喂入持久 TextDecoder 做标题判定（详见 parser.hpp 头注释）
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "readex/reader.hpp"

namespace readex {
namespace {

// 标题行长度上限（原始字节）。超长行必为正文，跳过标题判定（其字节仍喂入
// 解码器保持流状态）。约等于 170 个汉字，远大于任何真实标题。
const size_t kMaxTitleRawBytes = 512;

// allowDirectTail（无分隔符直接相连的标题余部）只对短行生效的上限（码点）。
// 让「第五章草木皆兵」「第十二章风雪夜归人」这类无空格标题通过，同时把
// 「第一章的内容并不复杂，只是我忘了」这类长正文行挡在外面。
// 已知残余误判（短正文行，如「第一章鱼」）见 parser.hpp 头注释。
const size_t kMaxDirectTailCps = 12;

// 标题总长度上限（码点，防御性；扫描器另有字节上限）。
const size_t kMaxTitleCps = 200;

// ---- UTF-8 基础工具（行文本都是解码后的 UTF-8）----

// 解码 pos 处一个码点；非法/截断序列按 U+FFFD 处理并前进 1 字节（与 decoder
// 的重同步策略一致）。返回消耗字节数（>=1），pos >= size 返回 0。
size_t utf8Next(const std::string& s, size_t pos, uint32_t& cp)
{
    const size_t len = s.size();
    if (pos >= len)
        return 0;
    const unsigned char b0 = static_cast<unsigned char>(s[pos]);
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }
    int need;
    uint32_t acc;
    unsigned char lo = 0x80, hi = 0xBF; // 第二字节合法范围（封杀过长/代理/越界）
    if ((b0 & 0xE0) == 0xC0) {
        need = 1; acc = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2; acc = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3; acc = b0 & 0x07u;
    } else {
        cp = 0xFFFD;
        return 1;
    }
    if (b0 == 0xE0) lo = 0xA0;
    if (b0 == 0xED) hi = 0x9F;
    if (b0 == 0xF0) lo = 0x90;
    if (b0 == 0xF4) hi = 0x8F;
    if (pos + static_cast<size_t>(need) >= len) {
        cp = 0xFFFD; // 截断（本函数只用于完整行文本）
        return 1;
    }
    for (int k = 1; k <= need; ++k) {
        const unsigned char bk = static_cast<unsigned char>(s[pos + k]);
        if (bk < lo || bk > hi) {
            cp = 0xFFFD;
            return 1;
        }
        acc = (acc << 6) | (bk & 0x3Fu);
        lo = 0x80;
        hi = 0xBF;
    }
    cp = acc;
    return static_cast<size_t>(need + 1);
}

size_t utf8CountCps(const std::string& s)
{
    size_t n = 0, pos = 0;
    uint32_t cp;
    while (pos < s.size()) {
        size_t step = utf8Next(s, pos, cp);
        if (step == 0)
            break;
        pos += step;
        ++n;
    }
    return n;
}

// ASCII 大小写不敏感前缀比较（非 ASCII 字节精确比较）。
bool startsWithAt(const std::string& s, size_t pos, const std::string& prefix,
                  bool ci)
{
    if (pos + prefix.size() > s.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(s[pos + i]);
        unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (ci && a < 0x80 && b < 0x80) {
            if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        }
        if (a != b)
            return false;
    }
    return true;
}

bool isAsciiSpace(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\v' || cp == '\f' ||
           cp == '\r' || cp == '\n';
}

// 可裁剪空白：ASCII 空白 + 全角空格 U+3000 + BOM/零宽 U+FEFF
bool isTrimSpace(uint32_t cp)
{
    return isAsciiSpace(cp) || cp == 0x3000 || cp == 0xFEFF;
}

// 中文数字（用于「第X章」数字与「番外一」类余部起始判定）
bool isCnNumeralCp(uint32_t cp)
{
    switch (cp) {
    case 0x3007: // 〇
    case 0x96F6: // 零
    case 0x4E00: // 一
    case 0x4E8C: // 二
    case 0x4E09: // 三
    case 0x56DB: // 四
    case 0x4E94: // 五
    case 0x516D: // 六
    case 0x4E03: // 七
    case 0x516B: // 八
    case 0x4E5D: // 九
    case 0x5341: // 十
    case 0x767E: // 百
    case 0x5343: // 千
    case 0x4E07: // 万
    case 0x4EBF: // 亿
    case 0x4E24: // 两
    case 0x62FE: // 拾
    case 0x4F70: // 佰
    case 0x4EDF: // 仟
    case 0x842C: // 萬
    case 0x5104: // 億
    case 0x58F9: // 壹
    case 0x8D30: // 贰
    case 0x53C1: // 叁
    case 0x8086: // 肆
    case 0x4F0D: // 伍
    case 0x9646: // 陆
    case 0x67D2: // 柒
    case 0x634C: // 捌
    case 0x7396: // 玖
        return true;
    default:
        return false;
    }
}

bool isDigitCp(uint32_t cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 0xFF10 && cp <= 0xFF19); // 含全角
}

bool isNumeralCp(uint32_t cp)
{
    return isDigitCp(cp) || isCnNumeralCp(cp);
}

// 「词字」：字母/数字/汉字/假名/谚文/全角字母数字等构成标题文字的字符；
// 其余（空白、标点、符号）都视为分隔符。
bool isWordCp(uint32_t cp)
{
    if (cp < 0x80)
        return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
               (cp >= 'a' && cp <= 'z') || cp == '_';
    if (isDigitCp(cp))
        return true;                     // 全角数字
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true; // CJK 统一表意
    if (cp >= 0x3400 && cp <= 0x4DBF) return true; // 扩展 A
    if (cp >= 0xF900 && cp <= 0xFAFF) return true; // 兼容表意
    if (cp >= 0x3040 && cp <= 0x30FF) return true; // 假名
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true; // 谚文
    if (cp >= 0xFF21 && cp <= 0xFF3A) return true; // 全角字母
    if (cp >= 0xFF41 && cp <= 0xFF5A) return true;
    if (cp == 0x3007) return true;       // 〇
    return false;
}

// 句末标点：以之结尾的行几乎必然是句子而非标题（正文防误判闸门 G1）
bool isSentenceFinalCp(uint32_t cp)
{
    switch (cp) {
    case 0x3002: // 。
    case 0xFF01: // ！
    case 0xFF1F: // ？
    case 0x2026: // …
    case 0xFF1B: // ；
    case ';':
    case '!':
    case '?':
        return true;
    default:
        return false;
    }
}

// 裁掉首尾空白，写回 [begin, end)
void trimRange(const std::string& s, size_t& begin, size_t& end)
{
    begin = 0;
    end = s.size();
    uint32_t cp;
    while (begin < end) {
        size_t n = utf8Next(s, begin, cp);
        if (n == 0 || !isTrimSpace(cp))
            break;
        begin += n;
    }
    while (end > begin) {
        size_t p = end - 1; // 回退到末码点的首字节
        while (p > begin && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
            --p;
        size_t n = utf8Next(s, p, cp);
        if (n == 0 || p + n != end || !isTrimSpace(cp))
            break;
        end = p;
    }
}

// 标题余部策略（正文防误判闸门 G2，见 parser.hpp 头注释）
bool tailAllowed(const std::string& line, size_t tailPos, size_t end,
                 bool allowSubtitle, bool allowDirectTail, size_t lineCps)
{
    if (tailPos >= end)
        return true; // pattern 恰好占满整行
    uint32_t cp;
    utf8Next(line, tailPos, cp);
    if (!isWordCp(cp))
        return allowSubtitle; // 分隔符/标点开头的标题余部
    if (isCnNumeralCp(cp))
        return true; // 「番外一」「第一章一梦千年」类：数字开头视为标题续接
    return allowDirectTail && lineCps <= kMaxDirectTailCps;
}

} // namespace

// ---- 默认规则表（唯一维护点）----
const std::vector<ChapterRule>& defaultChapterRules()
{
    static const std::vector<ChapterRule> rules = [] {
        std::vector<ChapterRule> v;
        // 「第X章 / 节 / 回 / 卷 / 篇 / 部 / 集 / 幕」
        const char* kSuffixes[] = { "章", "节", "回", "卷", "篇", "部", "集", "幕" };
        for (const char* suf : kSuffixes) {
            ChapterRule r;
            r.kind = ChapterRule::Kind::Numbered;
            r.prefix = "第";
            r.suffix = suf;
            v.push_back(r);
        }
        // 「Chapter X」（大小写不敏感，数字后可直接跟标题）
        {
            ChapterRule r;
            r.kind = ChapterRule::Kind::Numbered;
            r.prefix = "Chapter";
            r.suffix = "";
            v.push_back(r);
        }
        // 关键词标题：序章 / 楔子 / 番外 ……
        const char* kKeywords[] = {
            "序章", "序言", "序", "前言", "引子", "楔子",
            "番外", "番外篇", "尾声", "后记", "附录",
            "终章", "终卷", "大结局",
        };
        for (const char* kw : kKeywords) {
            ChapterRule r;
            r.kind = ChapterRule::Kind::Keyword;
            r.keyword = kw;
            r.allowSubtitle = true;
            r.allowDirectTail = false; // 关键词余部须以分隔符/中文数字开头
            v.push_back(r);
        }
        return v;
    }();
    return rules;
}

// ---- 标题匹配器（纯函数）----
bool matchChapterTitle(const std::string& lineUtf8,
                       const std::vector<ChapterRule>& rules,
                       std::string* outTitle)
{
    size_t begin = 0, end = 0;
    trimRange(lineUtf8, begin, end);
    if (begin >= end)
        return false; // 空行

    // 闸门 G1：句末标点结尾的行按正文句子处理
    {
        size_t p = end - 1;
        while (p > begin && (static_cast<unsigned char>(lineUtf8[p]) & 0xC0) == 0x80)
            --p;
        uint32_t cp;
        if (utf8Next(lineUtf8, p, cp) > 0 && isSentenceFinalCp(cp))
            return false;
    }

    const std::string trimmed = lineUtf8.substr(begin, end - begin);
    // 标题总长上限（防御性）
    if (utf8CountCps(trimmed) > kMaxTitleCps)
        return false;
    const size_t lineCps = utf8CountCps(trimmed);

    for (const ChapterRule& rule : rules) {
        size_t pos = begin;
        if (rule.kind == ChapterRule::Kind::Numbered) {
            if (!startsWithAt(lineUtf8, pos, rule.prefix, rule.caseInsensitive))
                continue;
            pos += rule.prefix.size();
            // 数字前允许空格（「第 12 章」）
            while (pos < end) {
                uint32_t cp;
                size_t n = utf8Next(lineUtf8, pos, cp);
                if (n == 0 || !(cp == ' ' || cp == '\t' || cp == 0x3000))
                    break;
                pos += n;
            }
            // 数字（阿拉伯/全角/中文，>=1 个）
            size_t numBegin = pos;
            size_t numEnd = pos;
            while (numEnd < end) {
                uint32_t cp;
                size_t n = utf8Next(lineUtf8, numEnd, cp);
                if (n == 0 || !isNumeralCp(cp))
                    break;
                numEnd += n;
            }
            if (numEnd == numBegin)
                continue; // 无数字
            // 后缀匹配：数字贪心，后缀失败时逐码点回退拆分（防御自定义规则里
            // 后缀与数字集重叠，如「第X千」）。
            // 数字后仅在后缀非空时跳空格（「第 12 章」）；后缀为空（Chapter X）
            // 时保留余部的前导空格作分隔符，否则「Chapter 12 The Return」的
            // 标题余部会被误判为无分隔符直连。
            bool matched = false;
            size_t split = numEnd;
            while (split > numBegin) {
                size_t p2 = split;
                if (!rule.suffix.empty()) {
                    while (p2 < end) {
                        uint32_t cp;
                        size_t n = utf8Next(lineUtf8, p2, cp);
                        if (n == 0 || !(cp == ' ' || cp == '\t' || cp == 0x3000))
                            break;
                        p2 += n;
                    }
                }
                if (startsWithAt(lineUtf8, p2, rule.suffix, rule.caseInsensitive)) {
                    pos = p2 + rule.suffix.size();
                    matched = true;
                    break;
                }
                size_t back = split - 1; // 回退一个码点重试
                while (back > numBegin &&
                       (static_cast<unsigned char>(lineUtf8[back]) & 0xC0) == 0x80)
                    --back;
                split = back;
            }
            if (!matched)
                continue;
        } else { // Keyword
            if (!startsWithAt(lineUtf8, pos, rule.keyword, rule.caseInsensitive))
                continue;
            pos += rule.keyword.size();
            // 整行恰为关键词时 tailAllowed（余部为空）恒真
        }

        if (tailAllowed(lineUtf8, pos, end, rule.allowSubtitle,
                        rule.allowDirectTail, lineCps)) {
            if (outTitle)
                *outTitle = trimmed;
            return true;
        }
    }
    return false;
}

// ---- 扫描器 ----
namespace {

struct TitleHit {
    uint64_t offset;
    std::string title;
};

bool isBlankText(const std::string& utf8)
{
    size_t b, e;
    trimRange(utf8, b, e);
    return b >= e;
}

} // namespace

Result<std::vector<ChapterInfo>> scanChapters(FileReader& reader,
                                              Encoding encoding,
                                              const std::string& fallbackTitle)
{
    return scanChapters(reader, encoding, fallbackTitle, defaultChapterRules());
}

Result<std::vector<ChapterInfo>> scanChapters(FileReader& reader,
                                              Encoding encoding,
                                              const std::string& fallbackTitle,
                                              const std::vector<ChapterRule>& rules)
{
    // 结束（含出错）恢复进入时的 reader 位置
    struct PosGuard {
        FileReader& r;
        uint64_t p;
        ~PosGuard() { (void)r.seek(p); }
    } guard{ reader, reader.tell() };

    Result<TextDecoder> dres = TextDecoder::create(encoding);
    if (!dres.ok())
        return Result<std::vector<ChapterInfo>>::failure(dres.error());
    TextDecoder decoder = std::move(dres.value());

    Result<void> sres = reader.seek(0);
    if (!sres.ok())
        return Result<std::vector<ChapterInfo>>::failure(sres.error());

    const uint64_t fsize = reader.size();
    std::vector<TitleHit> hits;

    // —— 行扫描状态 ——
    std::string lineAcc;  // 当前行的原始字节（封顶 kMaxTitleRawBytes）
    bool lineOverflow = false;
    uint64_t lineStart = 0; // 当前行首绝对偏移
    bool preambleNonBlank = false;

    auto feedDiscard = [&decoder](const char* data, size_t len) {
        (void)decoder.feed(data, len);
    };

    auto appendFrag = [&](const char* data, size_t len) {
        if (len == 0)
            return;
        if (lineOverflow) {
            feedDiscard(data, len);
            return;
        }
        if (lineAcc.size() + len <= kMaxTitleRawBytes) {
            lineAcc.append(data, len);
        } else {
            // 超长行：已累积部分 + 本片段全部喂入解码器（输出丢弃），
            // 标记 overflow，到行尾为止只喂不存
            lineOverflow = true;
            feedDiscard(lineAcc.data(), lineAcc.size());
            feedDiscard(data, len);
            lineAcc.clear();
        }
    };

    auto endLine = [&]() {
        if (!lineOverflow) {
            std::string lineOut = decoder.feed(lineAcc.data(), lineAcc.size());
            std::string title;
            if (matchChapterTitle(lineOut, rules, &title)) {
                hits.push_back(TitleHit{ lineStart, std::move(title) });
            } else if (hits.empty() && !preambleNonBlank && !isBlankText(lineOut)) {
                preambleNonBlank = true;
            }
        } else if (hits.empty()) {
            preambleNonBlank = true; // 超长行必为正文内容
        }
        lineAcc.clear();
        lineOverflow = false;
    };

    const bool utf16 =
        (encoding == Encoding::Utf16Le || encoding == Encoding::Utf16Be);
    const bool le = (encoding == Encoding::Utf16Le);
    // 注：kChunkSize 必须保持偶数——UTF-16 码元对齐依赖 chunk 首偏移为偶数
    //（从 0 起步、每次推进偶数字节），故码元不会跨 chunk 断开（断开只会
    // 发生在奇数大小文件的最后一字节，走行尾残余路径并入内容即可）。
    const size_t kChunkSize = 64 * 1024;
    std::string chunk(kChunkSize, '\0');
    uint64_t base = 0; // chunk 首字节绝对偏移

    for (;;) {
        Result<size_t> rres = reader.read(&chunk[0], kChunkSize);
        if (!rres.ok())
            return Result<std::vector<ChapterInfo>>::failure(rres.error());
        const size_t n = rres.value();
        const char* data = chunk.data();
        size_t fragIdx = 0; // 当前「行内未消费片段」在 data 中的起点

        if (!utf16) {
            // UTF-8 / GBK：0x0A 行界。多字节序列不可能包含 0x0A
            //（UTF-8 续字节 >= 0x80；GBK 次字节范围 0x40-0xFE 且 != 0x7F），
            // 字节层行切分不会切断字符。
            size_t i = 0;
            while (i < n) {
                const char* nl = static_cast<const char*>(
                    std::memchr(data + i, '\n', n - i));
                if (nl == nullptr) {
                    i = n;
                    break;
                }
                size_t nlIdx = static_cast<size_t>(nl - data);
                size_t contentEnd = nlIdx;
                if (contentEnd > fragIdx && data[contentEnd - 1] == '\r')
                    --contentEnd; // CR-LF：CR 属于行界
                appendFrag(data + fragIdx, contentEnd - fragIdx);
                endLine();
                i = nlIdx + 1;
                fragIdx = i;
                lineStart = base + i;
            }
        } else {
            // UTF-16：0x000A 码元行界（码元边界天然不切断字符）
            size_t i = 0;
            while (i + 1 < n) {
                const unsigned char b0 = static_cast<unsigned char>(data[i]);
                const unsigned char b1 = static_cast<unsigned char>(data[i + 1]);
                const bool isLf = le ? (b0 == 0x0A && b1 == 0x00)
                                     : (b0 == 0x00 && b1 == 0x0A);
                if (!isLf) {
                    i += 2;
                    continue;
                }
                size_t contentEnd = i; // 不含行界码元
                if (contentEnd >= fragIdx + 2) { // CR-LF：上一码元是 CR 则剔除
                    const unsigned char c0 =
                        static_cast<unsigned char>(data[contentEnd - 2]);
                    const unsigned char c1 =
                        static_cast<unsigned char>(data[contentEnd - 1]);
                    const bool isCr = le ? (c0 == 0x0D && c1 == 0x00)
                                         : (c0 == 0x00 && c1 == 0x0D);
                    if (isCr)
                        contentEnd -= 2;
                }
                appendFrag(data + fragIdx, contentEnd - fragIdx);
                endLine();
                i += 2;
                fragIdx = i;
                lineStart = base + i;
            }
        }

        // chunk 尾部：当前行的延续片段（含 UTF-16 奇数残余字节）
        if (fragIdx < n)
            appendFrag(data + fragIdx, n - fragIdx);
        base += n;

        if (n < kChunkSize)
            break; // EOF
    }
    // 收尾：最后一行（无行界结尾）
    endLine();
    (void)decoder.finish();

    // —— 组装章节（区间无缝无重覆盖 [0, fsize)）——
    std::vector<ChapterInfo> chapters;
    if (hits.empty()) {
        chapters.push_back(ChapterInfo{ fallbackTitle, 0, fsize, 0 });
    } else {
        size_t idx = 0;
        uint64_t firstStart = hits.front().offset;
        if (firstStart > 0) {
            if (preambleNonBlank) {
                // 标题前有正文内容：成为第 0 章（title = 书名）
                chapters.push_back(
                    ChapterInfo{ fallbackTitle, 0, hits.front().offset, idx++ });
            } else {
                firstStart = 0; // 纯空白前缀并入第一章
            }
        }
        for (size_t k = 0; k < hits.size(); ++k) {
            const uint64_t start = (k == 0) ? firstStart : hits[k].offset;
            const uint64_t end =
                (k + 1 < hits.size()) ? hits[k + 1].offset : fsize;
            chapters.push_back(ChapterInfo{ hits[k].title, start, end, idx++ });
        }
    }
    return Result<std::vector<ChapterInfo>>::success(std::move(chapters));
}

} // namespace readex
