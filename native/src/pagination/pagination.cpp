// pagination.cpp — 分页引擎实现（PR-4，计划书 §3.5）
//
// 算法（自行设计，未参考/移植任何现成分页实现）：
//   1. 单遍 UTF-8 扫描，把 content 切成「段落段」（\r\n / \n / \r 硬换行分隔），
//      段内逐码点贪心装行，行宽超可用宽时**向后回溯到最后一个合法断点**
//      （断点合法性 = 行首禁则 + 行尾禁则 + 英文词内不断 + 成对连字不断 +
//      空白后才断，见 codepointTraits / breakLegal），因此「，。」不落行首、
//      「「（」不落行尾靠提前换行实现；
//   2. 行按行高（fontSize × lineHeight）逐行装页，段落间加 paragraphSpacing
//      （段距不跨页：换页后新页页顶不补段距）；每页至少 1 行，单字超宽独占
//      一行（允许溢出可用行宽）——保证任何输入都推进、不丢字；
//   3. 页区间 = [该页首行起点, 下页首行起点)，末页到 content.size()，
//      行起点严格递增且首行起点为 0，故区间必然无缝无重拼接回原文；
//      硬换行符的字节归上一页区间（不显示，但偏移无缝）；
//   4. 行尾紧跟硬换行的空段（如 "abc\n" 的尾部）不再生成空行（与主流阅读器
//      一致），但其字节仍被页区间覆盖。
//
// 明确局限（详见 pagination.hpp 头注释）：无合法断点时强制断行或悬挂 1 字；
// 连续禁则字符在极窄行宽下仍可能落行首；无悬挂标点/断词/两端对齐。
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/pagination.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace readex {

namespace {

constexpr float kEps = 1e-3f; // 浮点比较容差（px 量级可忽略）

bool finitePositive(float v) { return std::isfinite(v) && v > 0; }
bool finiteNonNegative(float v) { return std::isfinite(v) && v >= 0; }

// ---- UTF-8 扫描 ----
struct CpUnit {
    uint32_t cp;   // 码点；非法序列时为 0xFFFD
    uint32_t len;  // 字节长度；非法序列时为 1（不丢字节）
};

// 解码 data[pos] 处码点；拒绝超长/截断/越界序列（按 WHATWG 风格前进 1 字节）
CpUnit decodeAt(const char* data, size_t size, size_t pos)
{
    const unsigned char b0 = static_cast<unsigned char>(data[pos]);
    if (b0 < 0x80)
        return { b0, 1 };

    uint32_t cp = 0;
    uint32_t len = 0;
    uint32_t minCp = 0;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        cp = b0 & 0x1Fu;
        len = 2;
        minCp = 0x80;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        cp = b0 & 0x0Fu;
        len = 3;
        minCp = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        cp = b0 & 0x07u;
        len = 4;
        minCp = 0x10000;
    } else {
        return { 0xFFFD, 1 };
    }
    if (pos + len > size)
        return { 0xFFFD, 1 };
    for (uint32_t k = 1; k < len; ++k) {
        const unsigned char bk = static_cast<unsigned char>(data[pos + k]);
        if ((bk & 0xC0u) != 0x80u)
            return { 0xFFFD, 1 };
        cp = (cp << 6) | (bk & 0x3Fu);
    }
    if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return { 0xFFFD, 1 };
    return { cp, len };
}

// ---- 行首/行尾禁则等规则表（排版常识自行整理，非移植）----
// 查找表按升序排列，用二分查找。

bool inSorted(const uint32_t* table, size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (table[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < n && table[lo] == cp;
}

// 行首禁则：收尾标点/收引号/小写假名等不落行首（JIS 避头尾常识子集）
const uint32_t kLineStartForbidden[] = {
    // ASCII: ! " # % ' ) * , . / : ; ? ] } ~
    0x21, 0x22, 0x23, 0x25, 0x27, 0x29, 0x2A, 0x2C, 0x2E, 0x2F, 0x3A, 0x3B,
    0x3F, 0x5D, 0x7D, 0x7E,
    // CJK 标点/收尾符号
    0x2013, 0x2014, 0x2015, 0x2019, 0x201D, 0x2025, 0x2026, 0x2030, 0x2103,
    0x3001, 0x3002, 0x3005, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015,
    0x3017, 0x3019, 0x301B, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063,
    0x3083, 0x3085, 0x3087, 0x308E, 0x3095, 0x3096, 0x309D, 0x309E, 0x30A1,
    0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE,
    0x30F5, 0x30F6, 0x30FB, 0x30FC, 0x30FD, 0x30FE,
    // 全角标点
    0xFF01, 0xFF02, 0xFF03, 0xFF05, 0xFF07, 0xFF09, 0xFF0A, 0xFF0C, 0xFF0E,
    0xFF0F, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF3D, 0xFF5D, 0xFF61, 0xFF64, 0xFF65,
};

// 行尾禁则：起始引号/开括号不落行尾
const uint32_t kLineEndForbidden[] = {
    // ASCII: ( [ { " '
    0x22, 0x27, 0x28, 0x5B, 0x7B,
    // CJK 开引号/开括号
    0x2018, 0x201C,
    0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0x3016, 0x3018, 0x301A,
    // 全角
    0xFF08, 0xFF3B, 0xFF5B, 0xFF5F,
};

// 成对连字：与相同字符相邻时中间不断（—— …… ～～ 等）
const uint32_t kPairChars[] = {
    0x7E, 0x2013, 0x2014, 0x2025, 0x2026, 0x3005, 0x30FC, 0xFF5E,
};

bool isLineStartForbidden(uint32_t cp)
{
    return inSorted(kLineStartForbidden,
                    sizeof(kLineStartForbidden) / sizeof(uint32_t), cp);
}
bool isLineEndForbidden(uint32_t cp)
{
    return inSorted(kLineEndForbidden,
                    sizeof(kLineEndForbidden) / sizeof(uint32_t), cp);
}
bool isPairChar(uint32_t cp)
{
    return inSorted(kPairChars, sizeof(kPairChars) / sizeof(uint32_t), cp);
}

// 引号类里方向不明的 ASCII " '：既不落行首也不落行尾（宁可留在行中，
// 顺带保证 don't 等缩写不在撇号处断开）；「『（《等明确起始引号/括号
// 可以正常落行首（仅行尾禁则），curly 引号 ‘ ’ “ ” 亦按方向明确处理。
bool isAmbiguousQuote(uint32_t cp)
{
    return cp == 0x22 || cp == 0x27;
}

// ---- 引擎内部行记录 ----
struct CharRec {
    uint64_t off;    // 码点首字节偏移
    uint32_t len;    // 字节长度
    uint32_t cp;     // 码点（非法字节为 0xFFFD）
    float adv;       // 行内前进宽 = 字形宽 + letterSpacing
    CharTraits traits;
};

bool breakLegal(const CharRec& a, const CharRec& b)
{
    if (!b.traits.canStartLine)
        return false; // 行首禁则
    if (!a.traits.canEndLine)
        return false; // 行尾禁则
    if (b.traits.isSpace)
        return false; // 空白不落行首：只在空白之后断
    if (a.traits.isWordChar && b.traits.isWordChar)
        return false; // 英文词内不断
    if (a.traits.isPairChar && b.traits.isPairChar && a.cp == b.cp)
        return false; // 成对连字中间不断
    return true;
}

// 分页主体（measurer 已就绪）
Result<std::vector<PageInfo>> paginateImpl(const std::string& content,
                                           const PaginationConfig& config,
                                           const TextMeasurer& measurer,
                                           bool checkEstimator)
{
    if (checkEstimator) {
        const EstimatorConfig& e = config.estimator;
        if (!finitePositive(e.asciiWidthEm) || !finitePositive(e.cjkWidthEm) ||
            !finitePositive(e.punctuationWidthEm))
            return Result<std::vector<PageInfo>>::failure(
                ErrorCode::InvalidArgument,
                "PaginationConfig.estimator 各 em 系数必须为有限正数");
    }

    const float availWidth =
        config.width - config.paddingLeft - config.paddingRight;
    const float availHeight =
        config.height - config.paddingTop - config.paddingBottom;
    const float pitch = config.fontSize * config.lineHeight;
    const float spacing = config.letterSpacing;
    const float paraSpacing = config.paragraphSpacing;

    std::vector<PageInfo> pages;
    std::vector<CharRec> cur; // 当前视觉行
    float curWidth = 0;
    uint64_t lineStart = 0; // 当前行首字节偏移

    // 页面垂直状态
    uint64_t pageStart = 0;
    float y = 0;
    bool pageHasLine = false;
    float pendingExtra = 0; // 段距（段落首行前附加；换页后不补）

    // 放置一行（行起点 start，行前附加 extra）
    auto placeLine = [&](uint64_t start, float extra) {
        float need = pitch + extra;
        if (pageHasLine && y + need > availHeight + kEps) {
            pages.push_back(PageInfo{ pageStart, start, pages.size() });
            pageStart = start;
            y = 0;
            pageHasLine = false;
            need = pitch; // 段距不跨页
        }
        y += need;
        pageHasLine = true;
    };

    // 关闭当前行：放置并重置；nextStart 为下一行起点
    auto closeLine = [&](uint64_t nextStart, bool hardBreak) {
        placeLine(lineStart, pendingExtra);
        pendingExtra = hardBreak ? paraSpacing : 0;
        cur.clear();
        curWidth = 0;
        lineStart = nextStart;
    };

    size_t pos = 0;
    while (pos < content.size()) {
        const CpUnit u = decodeAt(content.data(), content.size(), pos);
        if (u.cp == '\n' || u.cp == '\r') {
            // 硬换行：\r\n 视为一个
            uint32_t brkLen = u.len;
            if (u.cp == '\r' && pos + 1 < content.size() &&
                content[pos + 1] == '\n')
                brkLen = 2;
            closeLine(pos + brkLen, true);
            pos += brkLen;
            continue;
        }

        CharTraits tr = codepointTraits(u.cp);
        float glyph;
        if (u.cp == '\t')
            glyph = 4.0f * measurer.codepointWidth(' '); // \t 按 4 个半角空格宽
        else
            glyph = measurer.codepointWidth(u.cp); // 非法字节（cp=U+FFFD）走
                                                   // 测量器的半角/替换字形宽度

        CharRec rec{ pos, u.len, u.cp, glyph + spacing, tr };
        cur.push_back(rec);
        curWidth += rec.adv;

        if (curWidth > availWidth + kEps) {
            // 行超宽：回溯到最后一个「合法且剩余部分装得下」的断点
            const size_t i = cur.size() - 1;
            size_t wrapAt = 0;
            float suffix = 0;
            for (size_t p = i + 1; p-- > 1;) {
                suffix += cur[p].adv; // cur[p..i] 的行宽
                if (suffix > availWidth + kEps)
                    break; // 再往左只会更宽
                if (breakLegal(cur[p - 1], cur[p])) {
                    wrapAt = p;
                    break;
                }
            }
            if (wrapAt > 0) {
                const uint64_t next = cur[wrapAt].off;
                // 保留 cur[wrapAt..i] 作为新行
                std::vector<CharRec> rest(cur.begin() + static_cast<long>(wrapAt),
                                           cur.end());
                float restWidth = 0;
                for (const CharRec& r : rest)
                    restWidth += r.adv;
                closeLine(next, false);
                cur = std::move(rest);
                curWidth = restWidth;
            } else if (i == 0) {
                // 单字即超宽：独占一行（允许溢出可用行宽，保证推进）
                closeLine(rec.off + rec.len, false);
            } else if (!rec.traits.canStartLine) {
                // 行首禁则字悬挂：收进本行（最多悬 1 字）再换行
                closeLine(rec.off + rec.len, false);
            } else {
                // 无合法断点（超长词/组合）：强制断行，本字留在新行行首
                const CharRec keep = rec;
                closeLine(rec.off, false);
                cur.push_back(keep);
                curWidth = keep.adv;
            }
        }

        pos += u.len;
    }

    // 尾行：内容以硬换行结尾时不再补空行（"abc\n" 只显示一行）；
    // 行内字节已被上一次关闭（悬挂/单字独占行）收完时同样不再补行。
    // 空 content 不放置任何行，由下方兜底产出单个空页。
    if (!cur.empty() || lineStart < content.size())
        closeLine(content.size(), false);

    pages.push_back(PageInfo{ pageStart, content.size(), pages.size() });
    return Result<std::vector<PageInfo>>::success(std::move(pages));
}

} // namespace

// ---- 字符归类（估算模型用）----
CharClass classifyCodepoint(uint32_t cp) noexcept
{
    // 中文标点 / 全角标点 -> Punct
    if (cp >= 0x3000 && cp <= 0x303F)
        return CharClass::Punct;
    if (cp == 0x2013 || cp == 0x2014 || cp == 0x2015 || cp == 0x2018 ||
        cp == 0x2019 || cp == 0x201C || cp == 0x201D || cp == 0x2025 ||
        cp == 0x2026)
        return CharClass::Punct;
    if ((cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) ||
        (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65))
        return CharClass::Punct;
    if (cp >= 0xFE30 && cp <= 0xFE4F)
        return CharClass::Punct; // CJK 兼容形式（﹏︵︶…）

    // 全角 -> Full
    if (cp >= 0x1100 && cp <= 0x11FF)
        return CharClass::Full; // 谚文字母
    if (cp >= 0x2E80 && cp <= 0x2EFF)
        return CharClass::Full; // CJK 部首补充
    if (cp >= 0x3040 && cp <= 0x30FF) {
        if (cp == 0x30FB)
            return CharClass::Punct; // ・中点
        return CharClass::Full;      // 假名（含 ー 々族）
    }
    if (cp >= 0x3400 && cp <= 0x4DBF)
        return CharClass::Full; // CJK 扩展 A
    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return CharClass::Full; // CJK 统一汉字
    if (cp >= 0xA960 && cp <= 0xA97F)
        return CharClass::Full; // 谚文字母扩展 A
    if (cp >= 0xAC00 && cp <= 0xD7AF)
        return CharClass::Full; // 谚文音节
    if (cp >= 0xF900 && cp <= 0xFAFF)
        return CharClass::Full; // CJK 兼容汉字
    if (cp >= 0xFF10 && cp <= 0xFF3A)
        return CharClass::Full; // 全角数字/大写
    if (cp >= 0xFF41 && cp <= 0xFF5A)
        return CharClass::Full; // 全角小写
    if (cp >= 0xFF66 && cp <= 0xFF9F)
        return CharClass::Half; // 半角片假名
    if (cp >= 0x10000)
        return CharClass::Full; // 增补平面（含 emoji）按全宽估
    return CharClass::Half;     // 其余按半角
}

CharTraits codepointTraits(uint32_t cp) noexcept
{
    CharTraits t{ true, true, false, false, false, false };
    if (cp == '\n' || cp == '\r') {
        t.isHardBreak = true;
        return t;
    }
    if (cp == '\t' || cp == ' ' || cp == 0xA0 || cp == 0x1680 ||
        (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F ||
        cp == 0x3000) {
        t.isSpace = true;
        return t;
    }
    if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
        (cp >= 'a' && cp <= 'z'))
        t.isWordChar = true;

    if (isLineStartForbidden(cp) || isAmbiguousQuote(cp))
        t.canStartLine = false;
    if (isLineEndForbidden(cp) || isAmbiguousQuote(cp))
        t.canEndLine = false;
    if (isPairChar(cp))
        t.isPairChar = true;
    return t;
}

// ---- TextMeasurer ----
TextMeasurer::~TextMeasurer() = default;

float TextMeasurer::measure(const char* text, size_t bytes) const
{
    float w = 0;
    size_t pos = 0;
    while (pos < bytes) {
        const CpUnit u = decodeAt(text, bytes, pos); // 与引擎同一解码口径
        w += codepointWidth(u.cp);
        pos += u.len;
    }
    return w;
}

// ---- EstimatingMeasurer ----
EstimatingMeasurer::EstimatingMeasurer(float fontSize,
                                       const EstimatorConfig& estimator)
    : fontSize_(fontSize), estimator_(estimator)
{
}

float EstimatingMeasurer::codepointWidth(uint32_t cp) const
{
    switch (classifyCodepoint(cp)) {
    case CharClass::Full:
        return estimator_.cjkWidthEm * fontSize_;
    case CharClass::Punct:
        return estimator_.punctuationWidthEm * fontSize_;
    case CharClass::Half:
    default:
        return estimator_.asciiWidthEm * fontSize_;
    }
}

// ---- 配置校验 ----
Error validatePaginationConfig(const PaginationConfig& c)
{
    if (!finitePositive(c.width))
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig.width 必须为有限正数（可用阅读区域宽，"
                     "由 UI 显式传入）");
    if (!finitePositive(c.height))
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig.height 必须为有限正数（可用阅读区域高，"
                     "由 UI 显式传入）");
    if (!finitePositive(c.fontSize))
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig.fontSize 必须为有限正数");
    if (!finitePositive(c.lineHeight))
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig.lineHeight 必须为有限正数（倍数）");
    if (!finiteNonNegative(c.letterSpacing))
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig.letterSpacing 必须为有限非负数");
    if (!finiteNonNegative(c.paragraphSpacing))
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig.paragraphSpacing 必须为有限非负数");
    const float margins[4] = { c.paddingTop, c.paddingBottom, c.paddingLeft,
                               c.paddingRight };
    for (float m : margins) {
        if (!finiteNonNegative(m))
            return Error(ErrorCode::InvalidArgument,
                         "PaginationConfig 边距必须为有限非负数");
    }
    if (c.width - c.paddingLeft - c.paddingRight <= 0)
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig 左右边距吃光可用区宽度");
    if (c.height - c.paddingTop - c.paddingBottom <= 0)
        return Error(ErrorCode::InvalidArgument,
                     "PaginationConfig 上下边距吃光可用区高度");
    return Error();
}

// ---- PaginationEngine ----
Result<std::vector<PageInfo>>
PaginationEngine::paginate(const std::string& content,
                           const PaginationConfig& config)
{
    const Error err = validatePaginationConfig(config);
    if (!err.ok())
        return Result<std::vector<PageInfo>>::failure(err);
    const EstimatingMeasurer measurer(config.fontSize, config.estimator);
    return paginateImpl(content, config, measurer, true);
}

Result<std::vector<PageInfo>>
PaginationEngine::paginate(const std::string& content,
                           const PaginationConfig& config,
                           const TextMeasurer& measurer)
{
    const Error err = validatePaginationConfig(config);
    if (!err.ok())
        return Result<std::vector<PageInfo>>::failure(err);
    // 注入测量器时 config.estimator 不参与（也不校验）
    return paginateImpl(content, config, measurer, false);
}

} // namespace readex
