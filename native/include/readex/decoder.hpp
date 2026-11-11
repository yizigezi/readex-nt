// decoder.hpp — 文本编码识别与转换接口（PR-2 实现）
//
// 当前仅接口声明，无实现。任务范围见《Readex Native Core 原生化重构计划》§3.2：
//   - 第一阶段支持 UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE / GBK / GB2312
//   - 分块解码，多字节字符跨块不产生乱码
//   - 不只按文件头判编码；非法字节序列有明确处理策略
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_DECODER_HPP
#define READEX_DECODER_HPP

#include <cstddef>
#include <string>

namespace readex {

class FileReader; // 见 reader/（PR-2，流式读取，对接已有 fs 能力，不重复实现）

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
    // 采样识别编码（结合 BOM 与字节统计，不只看文件头）。
    static Encoding detect(FileReader& reader);

    // 分块解码：data/length 为一块原始字节，块边界可能截断多字节字符，
    // 实现需自行维护跨块状态；非法序列的处理策略在实现时补充并测试。
    static std::string decode(const char* data, size_t length, Encoding encoding);
};

} // namespace readex

#endif // READEX_DECODER_HPP
