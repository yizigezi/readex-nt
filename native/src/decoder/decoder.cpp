// decoder.cpp — 文本编码识别与转换实现（PR-2）
//
// UTF-8 / UTF-16 为手写增量转换；GBK 走 glibc iconv()（选型与风险见 decoder.hpp 头注释）。
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/decoder.hpp"

#include <iconv.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#include "readex/reader.hpp"

namespace readex {
namespace {

const char kReplacement[] = "\xEF\xBF\xBD"; // U+FFFD
const size_t kReplacementLen = 3;

// 把码点编码为 UTF-8 追加到 out（cp 已保证 < 0x110000）。
void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// 解码一个 UTF-8 字符。
// 返回：>0 = 消耗字节数；0 = 合法前缀被截断（留待后续字节）；-1 = 非法。
// 第二字节范围约束同时封杀过长序列 / 代理区 / 越界（E0: A0..BF、ED: 80..9F、
// F0: 90..BF、F4: 80..8F）。
int utf8Step(const unsigned char* p, size_t len, uint32_t& cp)
{
    if (len == 0)
        return 0;
    const unsigned char b0 = p[0];
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }

    int need;
    uint32_t acc;
    unsigned char lo = 0x80, hi = 0xBF;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        need = 1;
        acc = b0 & 0x1Fu;
    } else if (b0 == 0xE0) {
        need = 2;
        acc = b0 & 0x0Fu;
        lo = 0xA0;
    } else if (b0 >= 0xE1 && b0 <= 0xEC) {
        need = 2;
        acc = b0 & 0x0Fu;
    } else if (b0 == 0xED) {
        need = 2;
        acc = b0 & 0x0Fu;
        hi = 0x9F;
    } else if (b0 >= 0xEE && b0 <= 0xEF) {
        need = 2;
        acc = b0 & 0x0Fu;
    } else if (b0 == 0xF0) {
        need = 3;
        acc = b0 & 0x07u;
        lo = 0x90;
    } else if (b0 >= 0xF1 && b0 <= 0xF3) {
        need = 3;
        acc = b0 & 0x07u;
    } else if (b0 == 0xF4) {
        need = 3;
        acc = b0 & 0x07u;
        hi = 0x8F;
    } else {
        return -1; // 0x80-0xC1（孤立续字节/过长前导）、0xF5-0xFF
    }

    if (len < static_cast<size_t>(need) + 1) {
        // 已有字节必须是合法前缀，否则整体非法
        if (len >= 2 && (p[1] < lo || p[1] > hi))
            return -1;
        for (size_t k = 2; k < len; ++k) {
            if ((p[k] & 0xC0) != 0x80)
                return -1;
        }
        return 0;
    }

    if (p[1] < lo || p[1] > hi)
        return -1;
    for (int k = 1; k <= need; ++k) {
        if ((p[k] & 0xC0) != 0x80)
            return -1;
        acc = (acc << 6) | (p[k] & 0x3Fu);
    }
    cp = acc;
    return need + 1;
}

// 整窗 UTF-8 合法性（检测用）。skipLeadingCont：非文件头的采样窗口可能
// 从多字节字符中间开始，跳过开头的续字节；窗口末尾的截断前缀视为合法
// （那是采样边界造成的，不是文件本身非法）。
bool utf8ValidWindow(const unsigned char* p, size_t len, size_t skipLeadingCont)
{
    size_t i = 0;
    while (i < skipLeadingCont && i < len && (p[i] & 0xC0) == 0x80)
        ++i;
    while (i < len) {
        uint32_t cp = 0;
        int r = utf8Step(p + i, len - i, cp);
        if (r > 0)
            i += static_cast<size_t>(r);
        else if (r == 0)
            return true;
        else
            return false;
    }
    return true;
}

bool readWindow(FileReader& reader, uint64_t offset, uint64_t want, std::string& out)
{
    out.clear();
    if (!reader.seek(offset).ok())
        return false;
    char buf[8192];
    while (out.size() < want) {
        size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(sizeof buf, want - out.size()));
        Result<size_t> r = reader.read(buf, chunk);
        if (!r.ok())
            return false;
        if (r.value() == 0)
            break;
        out.append(buf, r.value());
    }
    return true;
}

} // namespace

TextDecoder::TextDecoder(Encoding encoding) noexcept
    : encoding_(encoding), iconv_(nullptr), hiSurrogate_(0), oddByte_(0),
      hasOddByte_(false), atStart_(true)
{
}

TextDecoder::~TextDecoder()
{
    if (iconv_ != nullptr)
        iconv_close(static_cast<iconv_t>(iconv_));
}

TextDecoder::TextDecoder(TextDecoder&& other) noexcept
    : encoding_(other.encoding_), iconv_(other.iconv_),
      pending_(std::move(other.pending_)), hiSurrogate_(other.hiSurrogate_),
      oddByte_(other.oddByte_), hasOddByte_(other.hasOddByte_),
      atStart_(other.atStart_)
{
    other.iconv_ = nullptr;
    other.hiSurrogate_ = 0;
    other.hasOddByte_ = false;
}

TextDecoder& TextDecoder::operator=(TextDecoder&& other) noexcept
{
    if (this == &other)
        return *this;
    if (iconv_ != nullptr)
        iconv_close(static_cast<iconv_t>(iconv_));
    encoding_ = other.encoding_;
    iconv_ = other.iconv_;
    pending_ = std::move(other.pending_);
    hiSurrogate_ = other.hiSurrogate_;
    oddByte_ = other.oddByte_;
    hasOddByte_ = other.hasOddByte_;
    atStart_ = other.atStart_;
    other.iconv_ = nullptr;
    other.hiSurrogate_ = 0;
    other.hasOddByte_ = false;
    return *this;
}

Encoding TextDecoder::encoding() const noexcept
{
    return encoding_;
}

Result<TextDecoder> TextDecoder::create(Encoding encoding)
{
    if (encoding == Encoding::Unknown)
        return Result<TextDecoder>::failure(ErrorCode::InvalidArgument,
                                            "TextDecoder::create: 未知编码");

    TextDecoder d(encoding);
    if (encoding == Encoding::Gbk) {
        // glibc 的 GBK 别名兼容性：优先 GBK，回退 CP936 / GB18030（GBK 超集，
        // 对 GBK/GB2312 输入解码结果一致，还能多容忍 GB18030 扩展字符）。
        static const char* kNames[] = { "GBK", "CP936", "GB18030" };
        const iconv_t kInvalid = (iconv_t)-1;
        iconv_t cd = kInvalid;
        for (const char* name : kNames) {
            cd = iconv_open("UTF-8", name);
            if (cd != kInvalid)
                break;
        }
        if (cd == kInvalid)
            return Result<TextDecoder>::failure(
                ErrorCode::DecodeError,
                "TextDecoder::create: 系统缺少 GBK 转换模块（iconv_open 失败）");
        d.iconv_ = cd;
    }
    return Result<TextDecoder>::success(std::move(d));
}

void TextDecoder::emitCp(std::string& out, uint32_t cp)
{
    // Utf8Bom/Utf16Le/Utf16Be：剥离开头的 U+FEFF（即 BOM），跨块也正确
    // （这里的 atStart_ 判定发生在第一个「解码出的码点」上，而非第一个字节上）。
    if (atStart_) {
        atStart_ = false;
        if (cp == 0xFEFF && encoding_ != Encoding::Utf8 && encoding_ != Encoding::Gbk)
            return;
    }
    appendUtf8(out, cp);
}

std::string TextDecoder::feed(const char* data, size_t length)
{
    if (data == nullptr || length == 0)
        return std::string();
    switch (encoding_) {
    case Encoding::Utf8:
    case Encoding::Utf8Bom:
        return feedUtf8(data, length);
    case Encoding::Utf16Le:
    case Encoding::Utf16Be:
        return feedUtf16(data, length);
    case Encoding::Gbk:
        return feedGbk(data, length);
    default:
        return std::string();
    }
}

std::string TextDecoder::finish()
{
    switch (encoding_) {
    case Encoding::Utf8:
    case Encoding::Utf8Bom:
        return finishUtf8();
    case Encoding::Utf16Le:
    case Encoding::Utf16Be:
        return finishUtf16();
    case Encoding::Gbk:
        return finishGbk();
    default:
        return std::string();
    }
}

std::string TextDecoder::feedUtf8(const char* data, size_t length)
{
    std::string out;
    std::string buf;
    const unsigned char* p;
    size_t avail;
    if (!pending_.empty()) {
        buf.reserve(pending_.size() + length);
        buf = pending_;
        buf.append(data, length);
        p = reinterpret_cast<const unsigned char*>(buf.data());
        avail = buf.size();
        pending_.clear();
    } else {
        p = reinterpret_cast<const unsigned char*>(data);
        avail = length;
    }

    size_t i = 0;
    while (i < avail) {
        uint32_t cp = 0;
        int r = utf8Step(p + i, avail - i, cp);
        if (r > 0) {
            emitCp(out, cp);
            i += static_cast<size_t>(r);
        } else if (r == 0) {
            // 合法前缀被截断：留待下一块
            pending_.assign(reinterpret_cast<const char*>(p + i), avail - i);
            break;
        } else {
            emitCp(out, 0xFFFD); // 策略：替换 + 前进 1 字节重同步
            ++i;
        }
    }
    return out;
}

std::string TextDecoder::finishUtf8()
{
    std::string out;
    if (!pending_.empty()) {
        out.append(kReplacement, kReplacementLen); // 残留未完成序列补一个 U+FFFD
        pending_.clear();
    }
    return out;
}

std::string TextDecoder::feedUtf16(const char* data, size_t length)
{
    std::string out;
    std::string buf;
    const unsigned char* p;
    size_t avail;
    if (hasOddByte_) {
        buf.reserve(1 + length);
        buf += static_cast<char>(oddByte_);
        buf.append(data, length);
        hasOddByte_ = false;
        p = reinterpret_cast<const unsigned char*>(buf.data());
        avail = buf.size();
    } else {
        p = reinterpret_cast<const unsigned char*>(data);
        avail = length;
    }

    const bool le = (encoding_ == Encoding::Utf16Le);
    size_t i = 0;
    while (i + 2 <= avail) {
        uint32_t unit = le ? (static_cast<uint32_t>(p[i]) |
                              (static_cast<uint32_t>(p[i + 1]) << 8))
                           : ((static_cast<uint32_t>(p[i]) << 8) |
                              static_cast<uint32_t>(p[i + 1]));
        i += 2;

        if (hiSurrogate_ != 0) {
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                uint32_t cp = 0x10000u + ((hiSurrogate_ - 0xD800u) << 10) +
                              (unit - 0xDC00u);
                hiSurrogate_ = 0;
                emitCp(out, cp);
                continue;
            }
            hiSurrogate_ = 0;
            emitCp(out, 0xFFFD); // 孤立高代理
        }
        if (unit >= 0xD800 && unit <= 0xDBFF)
            hiSurrogate_ = unit;
        else if (unit >= 0xDC00 && unit <= 0xDFFF)
            emitCp(out, 0xFFFD); // 孤立低代理
        else
            emitCp(out, unit);
    }
    if (i < avail) {
        oddByte_ = p[i];
        hasOddByte_ = true;
    }
    return out;
}

std::string TextDecoder::finishUtf16()
{
    std::string out;
    if (hiSurrogate_ != 0) {
        emitCp(out, 0xFFFD);
        hiSurrogate_ = 0;
    }
    if (hasOddByte_) {
        out.append(kReplacement, kReplacementLen); // 末尾落单字节
        hasOddByte_ = false;
    }
    return out;
}

std::string TextDecoder::feedGbk(const char* data, size_t length)
{
    std::string out;
    if (iconv_ == nullptr)
        return out; // 防御：create() 失败的对象不会被使用
    iconv_t cd = static_cast<iconv_t>(iconv_);

    std::string buf;
    const char* in;
    size_t inleft;
    if (!pending_.empty()) {
        buf.reserve(pending_.size() + length);
        buf = pending_;
        buf.append(data, length);
        in = buf.data();
        inleft = buf.size();
        pending_.clear();
    } else {
        in = data;
        inleft = length;
    }

    char outbuf[4096];
    while (inleft > 0) {
        char* inptr = const_cast<char*>(in);
        char* outp = outbuf;
        size_t outleft = sizeof outbuf;
        size_t r = iconv(cd, &inptr, &inleft, &outp, &outleft);
        in = inptr;
        out.append(outbuf, sizeof outbuf - outleft);

        if (r != static_cast<size_t>(-1))
            continue; // 本段输入消费完，由循环条件退出
        if (errno == E2BIG)
            continue; // 输出缓冲满，换一轮
        if (errno == EINVAL) {
            // 末尾未完成序列：留待下一块
            pending_.assign(in, inleft);
            inleft = 0;
            break;
        }
        // EILSEQ（或其他）：补 U+FFFD 并前进 1 字节重同步（策略 2），
        // 非法序列后的合法字节（如 0x81 0x20 里的空格）不会被吞掉。
        out.append(kReplacement, kReplacementLen);
        iconv(cd, nullptr, nullptr, nullptr, nullptr); // 重置移位状态（GBK 无移位，防御）
        if (inleft > 0) {
            ++in;
            --inleft;
        } else {
            break;
        }
    }
    return out;
}

std::string TextDecoder::finishGbk()
{
    std::string out;
    if (!pending_.empty()) {
        out.append(kReplacement, kReplacementLen); // 残留未完成序列补一个 U+FFFD
        pending_.clear();
    }
    if (iconv_ != nullptr)
        iconv(static_cast<iconv_t>(iconv_), nullptr, nullptr, nullptr, nullptr);
    return out;
}

Result<Encoding> TextDecoder::detect(FileReader& reader)
{
    // 采样期间挪动过 reader 位置，结束（含出错）时恢复
    struct PosGuard {
        FileReader& r;
        uint64_t p;
        ~PosGuard() { (void)r.seek(p); }
    } guard{ reader, reader.tell() };

    const uint64_t fsize = reader.size();
    if (fsize == 0)
        return Result<Encoding>::success(Encoding::Utf8);

    const uint64_t kWin = 64 * 1024;

    // 采样窗口：文件头 + 文件尾（尾窗起始对齐偶偏移，UTF-16 奇偶统计才正确）。
    // 小文件（≤64KiB）头窗即全文件——不是只看文件头。
    std::vector<std::pair<uint64_t, uint64_t>> wins; // (offset, length)
    wins.push_back(std::make_pair(uint64_t(0), std::min(kWin, fsize)));
    if (fsize > kWin) {
        uint64_t tailOff = std::max(kWin, fsize - kWin) & ~uint64_t(1);
        if (tailOff < fsize)
            wins.push_back(std::make_pair(tailOff, fsize - tailOff));
    }

    std::string sample;
    if (!readWindow(reader, wins[0].first, wins[0].second, sample))
        return Result<Encoding>::failure(ErrorCode::IoError,
                                         "detect: 读取采样失败");

    // 1) BOM 只在文件头判定
    const unsigned char* h = reinterpret_cast<const unsigned char*>(sample.data());
    const size_t hn = sample.size();
    if (hn >= 3 && h[0] == 0xEF && h[1] == 0xBB && h[2] == 0xBF)
        return Result<Encoding>::success(Encoding::Utf8Bom);
    if (hn >= 4 && h[0] == 0xFF && h[1] == 0xFE && h[2] == 0x00 && h[3] == 0x00)
        return Result<Encoding>::failure(ErrorCode::Unsupported,
                                         "detect: UTF-32 LE BOM，本阶段不支持 UTF-32");
    if (hn >= 4 && h[0] == 0x00 && h[1] == 0x00 && h[2] == 0xFE && h[3] == 0xFF)
        return Result<Encoding>::failure(ErrorCode::Unsupported,
                                         "detect: UTF-32 BE BOM，本阶段不支持 UTF-32");
    if (hn >= 2 && h[0] == 0xFF && h[1] == 0xFE)
        return Result<Encoding>::success(Encoding::Utf16Le);
    if (hn >= 2 && h[0] == 0xFE && h[1] == 0xFF)
        return Result<Encoding>::success(Encoding::Utf16Be);

    // 2)/3) 启发式统计（所有采样窗口汇总）
    uint64_t total = 0, evenZeros = 0, oddZeros = 0;
    bool utf8AllValid = true;

    for (size_t w = 0; w < wins.size(); ++w) {
        if (w > 0 && !readWindow(reader, wins[w].first, wins[w].second, sample))
            return Result<Encoding>::failure(ErrorCode::IoError,
                                             "detect: 读取采样失败");
        const unsigned char* p = reinterpret_cast<const unsigned char*>(sample.data());
        const size_t n = sample.size();
        const uint64_t off = wins[w].first;

        for (size_t i = 0; i < n; ++i) {
            if (p[i] == 0) {
                if ((off + i) & 1)
                    ++oddZeros;
                else
                    ++evenZeros;
            }
        }
        total += n;

        size_t skip = (w == 0) ? 0 : 3; // 尾窗可能从多字节字符中间开始
        if (!utf8ValidWindow(p, n, skip))
            utf8AllValid = false;
    }

    // 2) UTF-16 零字节奇偶模式。必须先于 UTF-8 判定：NUL 是合法 UTF-8 字节，
    //    无 BOM 的 ASCII 内容 UTF-16 会「看起来」是合法 UTF-8（隔字节 NUL）。
    //    已知局限：无 BOM 的纯中文 UTF-16 几乎没有零字节，会走到兜底 GBK。
    if (total >= 8) {
        const uint64_t minZeros = std::max<uint64_t>(2, total / 20);
        if (oddZeros >= minZeros && evenZeros * 8 < oddZeros)
            return Result<Encoding>::success(Encoding::Utf16Le);
        if (evenZeros >= minZeros && oddZeros * 8 < evenZeros)
            return Result<Encoding>::success(Encoding::Utf16Be);
    }

    // 3) UTF-8 合法性
    if (utf8AllValid)
        return Result<Encoding>::success(Encoding::Utf8);

    // 4) 兜底 GBK
    return Result<Encoding>::success(Encoding::Gbk);
}

std::string TextDecoder::decode(const char* data, size_t length, Encoding encoding)
{
    Result<TextDecoder> r = create(encoding);
    if (!r.ok())
        return std::string(); // 仅 GBK 缺 gconv 模块时发生，见头注释
    TextDecoder& d = r.value();
    std::string out = d.feed(data, length);
    out += d.finish();
    return out;
}

} // namespace readex
