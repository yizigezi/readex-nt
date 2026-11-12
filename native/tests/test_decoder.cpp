// test_decoder.cpp — TextDecoder 编码检测与分块解码（PR-2，计划书 §3.2/§七 编码）
//
// 覆盖：各编码检测（含无 BOM UTF-8/GBK）、解码正确性、中英标点混合、
// 分块边界扫描（每个切分位置结果一致）、非法输入不崩溃且按策略替换、
// BOM 剥离、UTF-32 BOM 不支持时明确报错。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"
#include "test_support.hpp"

#include "enc_fixtures.hpp"

#include "readex/decoder.hpp"
#include "readex/reader.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using readex::Encoding;
using readex::ErrorCode;
using readex::FileReader;
using readex::Result;
using readex::TextDecoder;

namespace {

const std::string kFFFD = "\xEF\xBF\xBD"; // U+FFFD

std::string oneShot(const std::string& bytes, Encoding enc)
{
    return TextDecoder::decode(bytes.data(), bytes.size(), enc);
}

// 按固定块大小喂解码器
std::string chunked(const std::string& bytes, Encoding enc, size_t chunk)
{
    Result<TextDecoder> d = TextDecoder::create(enc);
    if (!d.ok())
        return "<create failed>";
    std::string out;
    for (size_t i = 0; i < bytes.size(); i += chunk)
        out += d.value().feed(bytes.data() + i,
                              std::min(chunk, bytes.size() - i));
    out += d.value().finish();
    return out;
}

// 在 split 处切成两块喂解码器
std::string twoChunks(const std::string& bytes, Encoding enc, size_t split)
{
    Result<TextDecoder> d = TextDecoder::create(enc);
    if (!d.ok())
        return "<create failed>";
    std::string out = d.value().feed(bytes.data(), split);
    out += d.value().feed(bytes.data() + split, bytes.size() - split);
    out += d.value().finish();
    return out;
}

Result<Encoding> detectFile(const std::string& name, const std::string& bytes)
{
    std::string path = testsup::writeTestFile(name, bytes);
    FileReader r;
    if (!r.open(path).ok()) {
        testsup::removeTestFile(path);
        return Result<Encoding>::failure(ErrorCode::IoError, "测试文件打开失败");
    }
    Result<Encoding> res = TextDecoder::detect(r);
    r.close();
    testsup::removeTestFile(path);
    return res;
}

} // namespace

// ---------- 编码检测 ----------

TEST(decoder_detect_utf8_bom)
{
    Result<Encoding> e = detectFile("det_utf8bom.bin", encfix::utf8BomSample());
    CHECK(e.ok());
    CHECK(e.ok() && e.value() == Encoding::Utf8Bom);
}

TEST(decoder_detect_utf8_without_bom)
{
    Result<Encoding> e = detectFile("det_utf8.bin", encfix::utf8Sample());
    CHECK(e.ok() && e.value() == Encoding::Utf8);
}

TEST(decoder_detect_gbk_without_bom)
{
    Result<Encoding> e = detectFile("det_gbk.bin", encfix::gbkSample());
    CHECK(e.ok() && e.value() == Encoding::Gbk);
}

TEST(decoder_detect_ascii_is_utf8)
{
    // ASCII 同时是合法 UTF-8/GBK，检成 UTF-8（两种解读结果一致）
    Result<Encoding> e = detectFile("det_ascii.bin", "Hello, Readex 123.\n");
    CHECK(e.ok() && e.value() == Encoding::Utf8);
}

TEST(decoder_detect_empty_file_is_utf8)
{
    Result<Encoding> e = detectFile("det_empty.bin", "");
    CHECK(e.ok() && e.value() == Encoding::Utf8);
}

TEST(decoder_detect_utf16_boms)
{
    Result<Encoding> le = detectFile("det_utf16le_bom.bin", encfix::utf16LeBomSample());
    CHECK(le.ok() && le.value() == Encoding::Utf16Le);
    Result<Encoding> be = detectFile("det_utf16be_bom.bin", encfix::utf16BeBomSample());
    CHECK(be.ok() && be.value() == Encoding::Utf16Be);
}

TEST(decoder_detect_utf16_without_bom_zero_byte_pattern)
{
    Result<Encoding> le = detectFile("det_utf16le.bin", encfix::utf16LeSample());
    CHECK(le.ok() && le.value() == Encoding::Utf16Le);
    Result<Encoding> be = detectFile("det_utf16be.bin", encfix::utf16BeSample());
    CHECK(be.ok() && be.value() == Encoding::Utf16Be);
}

TEST(decoder_detect_utf32_bom_is_unsupported)
{
    std::string le = std::string("\xFF\xFE\x00\x00" "A\x00\x00\x00", 8);
    Result<Encoding> r1 = detectFile("det_utf32le.bin", le);
    CHECK(!r1.ok());
    CHECK(r1.error().code() == ErrorCode::Unsupported);

    std::string be = std::string("\x00\x00\xFE\xFF" "\x00\x00\x00" "A", 8);
    Result<Encoding> r2 = detectFile("det_utf32be.bin", be);
    CHECK(!r2.ok());
    CHECK(r2.error().code() == ErrorCode::Unsupported);
}

TEST(decoder_detect_restores_reader_position)
{
    std::string path = testsup::writeTestFile("det_pos.bin", encfix::gbkSample());
    FileReader r;
    CHECK(r.open(path).ok());
    CHECK(r.seek(5).ok());
    Result<Encoding> e = TextDecoder::detect(r);
    CHECK(e.ok() && e.value() == Encoding::Gbk);
    CHECK_EQ(r.tell(), uint64_t(5)); // 采样后恢复位置
    r.close();
    testsup::removeTestFile(path);
}

TEST(decoder_detect_long_file_is_not_header_only)
{
    // 头 64KiB 是纯 ASCII（碰巧合法 UTF-8），尾部是 GBK 中文：
    // 只看文件头会误判 UTF-8，采样尾窗才能兜底到 GBK
    std::string bytes(70000, 'A');
    std::string tail;
    for (int i = 0; i < 400; ++i)
        tail += encfix::punctGbkSample();
    bytes += tail;

    Result<Encoding> e = detectFile("det_long_gbk.bin", bytes);
    CHECK(e.ok() && e.value() == Encoding::Gbk);

    // 检测为 GBK 后整段解码应还原（头部 70000 个 'A' + 尾部标点文本 × 400）
    std::string decoded = oneShot(bytes, Encoding::Gbk);
    std::string want = encfix::punctText();
    bool allMatch = decoded.size() == 70000 + want.size() * 400;
    for (size_t i = 0; i < 70000 && allMatch; ++i) {
        if (decoded[i] != 'A')
            allMatch = false;
    }
    for (int i = 0; allMatch && i < 400; ++i) {
        if (decoded.compare(70000 + want.size() * i, want.size(), want) != 0)
            allMatch = false;
    }
    CHECK(allMatch);
}

// ---------- 解码正确性 ----------

TEST(decoder_decode_all_encodings_exact)
{
    const std::string want(encfix::sampleText());
    CHECK(oneShot(encfix::utf8Sample(), Encoding::Utf8) == want);
    CHECK(oneShot(encfix::utf8BomSample(), Encoding::Utf8Bom) == want);
    CHECK(oneShot(encfix::utf8Sample(), Encoding::Utf8Bom) == want); // 无 BOM 也正常
    CHECK(oneShot(encfix::utf16LeSample(), Encoding::Utf16Le) == want);
    CHECK(oneShot(encfix::utf16BeSample(), Encoding::Utf16Be) == want);
    CHECK(oneShot(encfix::utf16LeBomSample(), Encoding::Utf16Le) == want);
    CHECK(oneShot(encfix::utf16BeBomSample(), Encoding::Utf16Be) == want);
    CHECK(oneShot(encfix::gbkSample(), Encoding::Gbk) == want);
}

TEST(decoder_bom_stripping)
{
    const std::string want(encfix::sampleText());

    // Utf8Bom：剥离开头 U+FEFF（含逐字节喂入的跨块场景）
    CHECK(oneShot(encfix::utf8BomSample(), Encoding::Utf8Bom) == want);
    CHECK(chunked(encfix::utf8BomSample(), Encoding::Utf8Bom, 1) == want);

    // Utf8（无 BOM 语义）：保留 U+FEFF 原样
    std::string kept = oneShot(encfix::utf8BomSample(), Encoding::Utf8);
    CHECK(kept == std::string("\xEF\xBB\xBF") + want);

    // UTF-16 BOM 剥离同样跨块正确
    CHECK(chunked(encfix::utf16LeBomSample(), Encoding::Utf16Le, 1) == want);
    CHECK(chunked(encfix::utf16BeBomSample(), Encoding::Utf16Be, 3) == want);
}

// ---------- 分块边界扫描 ----------

TEST(decoder_chunk_scan_every_split_position)
{
    const std::string want(encfix::sampleText());
    struct Case {
        std::string bytes;
        Encoding enc;
    };
    const Case cases[] = {
        { encfix::utf8Sample(), Encoding::Utf8 },
        { encfix::utf8BomSample(), Encoding::Utf8Bom },
        { encfix::utf16LeSample(), Encoding::Utf16Le },
        { encfix::utf16BeSample(), Encoding::Utf16Be },
        { encfix::utf16LeBomSample(), Encoding::Utf16Le },
        { encfix::utf16BeBomSample(), Encoding::Utf16Be },
        { encfix::gbkSample(), Encoding::Gbk },
    };
    for (const Case& c : cases) {
        for (size_t split = 1; split < c.bytes.size(); ++split) {
            if (twoChunks(c.bytes, c.enc, split) != want) {
                CHECK(false); // 某切分位置结果不一致
                std::printf("    (编码=%d split=%zu)\n", int(c.enc), split);
                break;
            }
        }
    }
    CHECK(true);
}

TEST(decoder_chunk_scan_small_chunk_sizes)
{
    const std::string want(encfix::sampleText());
    struct Case {
        std::string bytes;
        Encoding enc;
    };
    const Case cases[] = {
        { encfix::utf8Sample(), Encoding::Utf8 },
        { encfix::utf16LeSample(), Encoding::Utf16Le },
        { encfix::utf16BeSample(), Encoding::Utf16Be },
        { encfix::gbkSample(), Encoding::Gbk },
    };
    for (const Case& c : cases) {
        for (size_t chunk = 1; chunk <= 13; ++chunk) {
            if (chunked(c.bytes, c.enc, chunk) != want) {
                CHECK(false);
                std::printf("    (编码=%d chunk=%zu)\n", int(c.enc), chunk);
                break;
            }
        }
    }
    CHECK(true);
}

TEST(decoder_punctuation_never_truncated)
{
    // 中文标点（，。！？「」——…《》（））在任何切分位置都不能截断/乱码
    const std::string bytes = encfix::punctGbkSample();
    const std::string want(encfix::punctText());
    CHECK(oneShot(bytes, Encoding::Gbk) == want);
    for (size_t split = 1; split < bytes.size(); ++split) {
        if (twoChunks(bytes, Encoding::Gbk, split) != want) {
            CHECK(false);
            std::printf("    (split=%zu)\n", split);
            break;
        }
    }
    // UTF-8 / UTF-16 标点同样扫描（样本正文里含 ——，。！？「」…《》）
    CHECK(chunked(encfix::utf8Sample(), Encoding::Utf8, 2) ==
          std::string(encfix::sampleText()));
    CHECK(chunked(encfix::utf16LeSample(), Encoding::Utf16Le, 1) ==
          std::string(encfix::sampleText()));
    CHECK(true);
}

// ---------- 非法输入策略（替换 U+FFFD，不崩溃） ----------

TEST(decoder_invalid_utf8_replacement_policy)
{
    // 策略：每处非法序列补一个 U+FFFD 并前进 1 字节重同步
    {
        const char raw[] = { 0x41, char(0xFF), 0x42 }; // A + 非法 + B
        CHECK(oneShot(std::string(raw, 3), Encoding::Utf8) ==
              std::string("A") + kFFFD + "B");
    }
    {
        const char raw[] = { char(0xC0), char(0x80) }; // 过长序列：两个非法前导
        CHECK(oneShot(std::string(raw, 2), Encoding::Utf8) == kFFFD + kFFFD);
    }
    {
        const char raw[] = { char(0xED), char(0xA0), char(0x80) }; // 代理区
        CHECK(oneShot(std::string(raw, 3), Encoding::Utf8) ==
              kFFFD + kFFFD + kFFFD);
    }
    {
        const char raw[] = { char(0xE4), char(0xBD), 0x20 }; // 假前缀 + 合法字节不吞
        CHECK(oneShot(std::string(raw, 3), Encoding::Utf8) ==
              kFFFD + kFFFD + " ");
    }
}

TEST(decoder_utf8_truncated_tail_finish_replaces)
{
    {
        const char raw[] = { char(0xE4), char(0xBD) }; // 「你」缺尾字节
        Result<TextDecoder> d = TextDecoder::create(Encoding::Utf8);
        CHECK(d.ok());
        CHECK(d.value().feed(raw, 2).empty()); // 未完成序列不输出
        CHECK(d.value().finish() == kFFFD);
    }
    {
        const char raw[] = { char(0xF0), char(0x9F), char(0x98) }; // 4 字节缺 1
        Result<TextDecoder> d = TextDecoder::create(Encoding::Utf8);
        CHECK(d.ok());
        std::string out = d.value().feed(raw, 3);
        out += d.value().finish();
        CHECK(out == kFFFD);
    }
}

TEST(decoder_invalid_gbk_replacement_policy)
{
    // iconv EILSEQ：补 U+FFFD 前进 1 字节，非法序列后的合法字节不被吞
    {
        const char raw[] = { char(0x81), 0x20 }; // 0x81 是 GBK 前导但 0x20 不是合法尾字节
        CHECK(oneShot(std::string(raw, 2), Encoding::Gbk) == kFFFD + " ");
    }
    {
        const char raw[] = { char(0xFF), 0x41 };
        CHECK(oneShot(std::string(raw, 2), Encoding::Gbk) == kFFFD + "A");
    }
    {
        const char raw[] = { char(0x81) }; // 末尾未完成序列
        Result<TextDecoder> d = TextDecoder::create(Encoding::Gbk);
        CHECK(d.ok());
        CHECK(d.value().feed(raw, 1).empty());
        CHECK(d.value().finish() == kFFFD);
    }
}

TEST(decoder_utf16_lone_surrogates_and_odd_tail)
{
    {
        const char raw[] = { 0x00, char(0xD8), 0x41, 0x00 }; // 孤立高代理 + 'A'
        CHECK(oneShot(std::string(raw, 4), Encoding::Utf16Le) == kFFFD + "A");
    }
    {
        const char raw[] = { 0x41, 0x00, 0x00, char(0xD8) }; // 'A' + 末尾孤立高代理
        CHECK(oneShot(std::string(raw, 4), Encoding::Utf16Le) == "A" + kFFFD);
    }
    {
        const char raw[] = { char(0xDC), 0x00, 0x00, 0x42 }; // 孤立低代理 + 'B'（BE）
        CHECK(oneShot(std::string(raw, 4), Encoding::Utf16Be) == kFFFD + "B");
    }
    {
        const char raw[] = { 0x41 }; // 末尾落单字节
        CHECK(oneShot(std::string(raw, 1), Encoding::Utf16Le) == kFFFD);
    }
    {
        // 代理对正常拼出码点 U+1F600（😀）
        const char raw[] = { 0x3D, char(0xD8), 0x00, char(0xDE) };
        CHECK(oneShot(std::string(raw, 4), Encoding::Utf16Le) ==
              std::string("\xF0\x9F\x98\x80"));
    }
}

TEST(decoder_chunk_scan_invalid_input_stable)
{
    // 含非法序列的输入在任何切分下结果也必须一致（重同步策略与块边界无关）
    struct Case {
        std::string bytes;
        Encoding enc;
    };
    const char rawUtf8[] = { 0x41, char(0xE4), 0x20, char(0xFF), char(0x80), 0x42 };
    const char rawGbk[] = { char(0x81), 0x20, char(0xD6), char(0xD0), char(0xFF) };
    const char rawUtf16[] = { 0x00, char(0xD8), 0x41, 0x00, 0x43 };
    const Case cases[] = {
        { std::string(rawUtf8, sizeof rawUtf8), Encoding::Utf8 },
        { std::string(rawGbk, sizeof rawGbk), Encoding::Gbk },
        { std::string(rawUtf16, sizeof rawUtf16), Encoding::Utf16Le },
    };
    for (const Case& c : cases) {
        std::string want = oneShot(c.bytes, c.enc);
        for (size_t split = 1; split < c.bytes.size(); ++split) {
            if (twoChunks(c.bytes, c.enc, split) != want) {
                CHECK(false);
                std::printf("    (编码=%d split=%zu)\n", int(c.enc), split);
                break;
            }
        }
    }
    CHECK(true);
}

TEST(decoder_empty_and_null_input_is_safe)
{
    Result<TextDecoder> d = TextDecoder::create(Encoding::Utf8);
    CHECK(d.ok());
    CHECK(d.value().feed(nullptr, 0).empty());
    CHECK(d.value().feed("", 0).empty());
    CHECK(d.value().feed("abc", 3) == "abc");
    CHECK(d.value().finish().empty());
    CHECK(oneShot("", Encoding::Utf8).empty());
    CHECK(oneShot("", Encoding::Gbk).empty());
}

TEST(decoder_create_unknown_encoding_fails)
{
    Result<TextDecoder> d = TextDecoder::create(Encoding::Unknown);
    CHECK(!d.ok());
    CHECK(d.error().code() == ErrorCode::InvalidArgument);
    CHECK(oneShot("x", Encoding::Unknown).empty());
}

// ---------- 性能探针（§八：只记录数据，不做阈值断言） ----------

TEST(decoder_perf_probe)
{
    // 4MiB 混合文本：流式读 + GBK 解码 + 编码检测的耗时（host，非设备指标）
    std::string block;
    for (int i = 0; i < 20000; ++i)
        block += encfix::gbkSample();
    std::string path = testsup::writeTestFile("perf_probe.bin", block);

    FileReader r;
    CHECK(r.open(path).ok());

    auto t0 = std::chrono::steady_clock::now();
    Result<Encoding> e = TextDecoder::detect(r);
    auto t1 = std::chrono::steady_clock::now();
    CHECK(e.ok() && e.value() == Encoding::Gbk);

    Result<TextDecoder> d = TextDecoder::create(e.value());
    CHECK(d.ok());
    std::string out;
    char buf[65536];
    size_t decodedBytes = 0;
    for (;;) {
        Result<size_t> n = r.read(buf, sizeof buf);
        CHECK(n.ok());
        if (!n.ok() || n.value() == 0)
            break;
        decodedBytes += n.value();
        out += d.value().feed(buf, n.value());
    }
    out += d.value().finish();
    auto t2 = std::chrono::steady_clock::now();

    CHECK_EQ(decodedBytes, block.size());

    double mb = double(block.size()) / (1024.0 * 1024.0);
    double detectMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double decodeMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::printf("    [perf] %.1f MiB: detect %.1f ms, 流式读+GBK 解码 %.1f ms (%.1f MiB/s)\n",
                mb, detectMs, decodeMs, decodeMs > 0 ? mb * 1000.0 / decodeMs : 0.0);

    r.close();
    testsup::removeTestFile(path);
}
