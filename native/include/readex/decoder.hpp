// decoder.hpp — 文本编码识别与转换（PR-2，计划书 §3.2 Text Decoder）
//
// 支持编码：UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE / GBK（含 GB2312）。
//
// —— 与计划书建议接口的偏差及理由 ——
// 计划书 §3.2 给的是一次性签名
//     std::string decode(const char* data, size_t length, Encoding encoding)
// 但「分块解码、多字节字符跨块不错乱」的验收要求解码器必须跨块保留未完成
// 序列的状态，一次性无状态函数做不到。因此本头以**有状态解码器**
// （create / feed / finish）为真实接口；计划书原签名保留为一次性便捷包装
// （内部等价 create + feed + finish），分块场景请使用有状态接口。
// detect() 返回 Result<Encoding>（计划书为裸 Encoding），以便表达读文件失败
// 与「识别出但不支持的编码」（如 UTF-32 BOM），错误仍走统一 Error 体系。
//
// —— 非法字节序列处理策略（明确且被测试覆盖）——
//   1. 非法序列一律替换为 U+FFFD（UTF-8: EF BF BD），不抛异常、不崩溃、不死循环；
//   2. UTF-8 / GBK：每遇一处非法字节，输出一个 U+FFFD 并**前进 1 字节**重新同步
//      （与 WHATWG 编码标准的错误恢复方向一致：非法序列后的合法字节不被吞掉）；
//   3. UTF-16：孤立代理项（不成对的高/低代理）替换为 U+FFFD；末尾落单字节在
//      finish() 时补一个 U+FFFD；
//   4. 块末尾未完成但前缀合法的多字节序列**不输出、不判非法**，保留到下一块；
//      finish() 时仍不完整的，整体补一个 U+FFFD；
//   5. 中文标点（，。！？「」——…《》）按普通字符处理，任何切分位置均不截断。
//
// —— GBK 转换选型 ——
// GBK -> UTF-8 走 glibc 内建 iconv()（libc.so.6 自带，不新增 ELF NEEDED，
// 天然 ARMv7 / glibc 2.23 兼容）。已知风险：极简 rootfs 若未安装 gconv 模块
// （/usr/lib/gconv），iconv_open 会失败，此时 create() 返回 DecodeError、
// 一次性 decode() 返回空串；届时需改为内嵌 GBK 码表（回退方案，见 PR-2 报告）。
// UTF-8 / UTF-16 为手写增量转换（无外部依赖，且 UTF-16 要处理代理对与 BOM）。
//
// —— detect() 启发式（不只看文件头，采样头尾各 ≤64KiB）——
//   BOM -> UTF-16 零字节奇偶模式 -> UTF-8 合法性 -> 兜底 GBK。
//   已知局限：无 BOM 的纯中文 UTF-16 文件（无 ASCII 则几乎无零字节）无法与
//   GBK 可靠区分，会兜底成 GBK；请优先使用带 BOM 的 UTF-16 文件。
//   BOM 剥离：Utf8Bom/Utf16Le/Utf16Be 解码时剥离开头的 U+FEFF（跨块也正确）。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_DECODER_HPP
#define READEX_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "readex/error.hpp"

namespace readex {

class FileReader; // 见 reader.hpp

enum class Encoding {
    Unknown = 0,
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Gbk, // 含 GB2312
};

class TextDecoder {
public:
    // 创建有状态解码器。Gbk 依赖系统 iconv，创建失败返回 DecodeError；
    // 其余编码为手写实现，不会失败。Unknown -> InvalidArgument。
    static Result<TextDecoder> create(Encoding encoding);
    ~TextDecoder();
    TextDecoder(TextDecoder&& other) noexcept;
    TextDecoder& operator=(TextDecoder&& other) noexcept;
    TextDecoder(const TextDecoder&) = delete;
    TextDecoder& operator=(const TextDecoder&) = delete;

    // 输入一块原始字节（块边界可截断多字节字符），返回当前可安全输出的
    // UTF-8 文本；未完成序列留在内部。
    std::string feed(const char* data, size_t length);

    // 流结束：内部残留的未完成序列补一个 U+FFFD（见上方策略 3/4），返回尾部。
    // finish() 后对象可继续 feed（等价于重新开始一段流，BOM 剥离不再发生）。
    std::string finish();

    Encoding encoding() const noexcept;

    // 采样识别编码（BOM / 字节统计 / UTF-8 合法性 / 兜底 GBK）。
    // 结束后把 reader 恢复到进入时的位置。
    static Result<Encoding> detect(FileReader& reader);

    // 一次性便捷包装（计划书 §3.2 原签名）：等价 create + feed + finish。
    // 仅当解码器建立失败（系统缺 GBK gconv 模块）才返回空串，
    // 需要区分该错误请使用 create() 的 Result 通道。
    static std::string decode(const char* data, size_t length, Encoding encoding);

private:
    explicit TextDecoder(Encoding encoding) noexcept;

    std::string feedUtf8(const char* data, size_t length);
    std::string feedUtf16(const char* data, size_t length);
    std::string feedGbk(const char* data, size_t length);
    std::string finishUtf8();
    std::string finishUtf16();
    std::string finishGbk();

    // 输出一个码点；处理流首 U+FEFF 剥离（Utf8Bom/Utf16*）。
    void emitCp(std::string& out, uint32_t cp);

    Encoding encoding_;
    void* iconv_;          // iconv_t（glibc 为 void*），仅 Gbk 使用
    std::string pending_;  // UTF-8/GBK 跨块残留字节（未完成序列）
    unsigned hiSurrogate_; // UTF-16 待配对高代理，0 = 无
    unsigned char oddByte_;
    bool hasOddByte_;
    bool atStart_;         // 尚未输出任何码点（BOM 剥离判定用）
};

// 编码英文标识（诊断与 JS 绑定层用）："utf-8" / "utf-8-bom" / "utf-16le" /
// "utf-16be" / "gbk" / "unknown"。PR-3 增补。
const char* encodingName(Encoding encoding) noexcept;

} // namespace readex

#endif // READEX_DECODER_HPP
