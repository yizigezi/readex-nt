// reading_state.hpp — 阅读状态：进度 / 偏移↔页映射 / 书签 / 序列化（PR-5，计划书 §3.7）
//
// —— 偏移语义（与 pagination.hpp / search.hpp 同一把尺子，勿改）——
//   ReadingState.textOffset / Bookmark.textOffset 是「章节**已解码文本**（UTF-8）
//   内的字节偏移」，恒应落在码点边界；不是原始文件字节偏移（那是 ChapterInfo
//   的语义）。textOffset 是定位的**唯一事实源**，pageIndex 是按当前分页配置算出
//   的派生缓存：换分页配置后按 textOffset 经新页区间重映射即可恢复位置。
//
// —— 与计划书 §3.7 建议结构的偏差及理由 ——
//   计划书写 fileOffset，本实现取 textOffset：章节扫描的文件偏移无法直接当
//   「阅读位置」用（解码后文本偏移才是分页/搜索/显示共用的坐标），且 PR-4 已
//   冻结「ReadingState = chapterIndex + textOffset」约定（见 pagination.hpp
//   头注释）。文件偏移如需换算，经 getChapter 文本长度另行处理，不在本模块。
//
// —— 持久化边界（计划书明确）——
//   原生只负责数据结构与序列化（serialize/deserialize，JSON 字符串，格式见下），
//   **持久化由 JS 存储层负责**（写文件/存 localStorage 等都是 JS 层的事）。
//
// —— 序列化格式（version 1，定一种）——
//   {
//     "version": 1,
//     "progress": { "chapterIndex": 0, "pageIndex": 0, "textOffset": 0 },
//     "bookmarks": [
//       { "chapterIndex": 0, "textOffset": 0, "note": "", "createdAt": "" }
//     ]
//   }
//   - 数字均为非负整数（十进制，无小数/指数；解析直接进 uint64，不受
//     JS Number 2^53 限制）；size_t 字段超过平台 SIZE_MAX 判非法；
//   - 字符串按 JSON 转义规则（\" \\ \/ \b \f \n \r \t \uXXXX），UTF-8 原样保留；
//   - 解析：结构/类型非法 -> ParseError；version 缺失或不认识 -> ParseError；
//     未知顶层/条目字段**忽略**（向前兼容）；多余尾随内容 -> ParseError；
//   - deserialize 只做结构与数值范围校验，**语义校验**（章节存在、偏移在章内
//     且在码点边界）由 setProgress/addBookmark 做，或由绑定层加载后再校验；
//   - 失败时 store 内容保持原样（先解析到临时变量，成功才整体替换）。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_READING_STATE_HPP
#define READEX_READING_STATE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "readex/error.hpp"
#include "readex/pagination.hpp" // PageInfo（页区间反查）

namespace readex {

class Book; // 校验需要读章节（book.hpp 在 reading_state.cpp 内包含）

// 阅读进度（计划书 §3.7 结构，fileOffset 换成 textOffset，见头注释）
struct ReadingState {
    size_t chapterIndex = 0;
    size_t pageIndex = 0;
    uint64_t textOffset = 0; // 章内已解码文本（UTF-8）字节偏移（码点边界）
};

// 书签：位置 + 可选备注/创建时间。createdAt 是 ISO-8601 之类的自由文本，
// 由调用方（JS 层）填写——原生层不取时钟、不解释其格式，只存取与序列化。
struct Bookmark {
    size_t chapterIndex = 0;
    uint64_t textOffset = 0;
    std::string note;
    std::string createdAt;
};

// ---- 纯映射函数（页区间反查 / 正查）----
// textOffset -> 所在页：返回包含 off 的页 [start, end)；off == 末页终点（文末
// 光标）归末页；空页表 / off 越出页表覆盖范围 -> InvalidArgument。
// pages 须为同一章文本 + 某分页配置下的 PageInfo[]（无缝拼接，PR-4 性质）。
Result<size_t> pageIndexForOffset(const std::vector<PageInfo>& pages,
                                  uint64_t textOffset);

// pageIndex -> 页起始偏移；越界 -> InvalidArgument。
Result<uint64_t> offsetForPage(const std::vector<PageInfo>& pages,
                               size_t pageIndex);

// 位置语义校验（setProgress / addBookmark 同款，绑定层加载序列化状态后可整体复核）：
// 章节号越界 / textOffset 越出该章解码文本 / 不在码点边界 -> InvalidArgument；
// Book 已 close -> InvalidHandle。
Result<void> validateStateLocation(const Book& book, size_t chapterIndex,
                                   uint64_t textOffset);

// ---- 阅读状态容器（每本书一份，Book 持有；纯数据 + 校验，不碰持久化）----
class ReadingStateStore {
public:
    ReadingStateStore() = default;

    // —— 进度 ——
    const ReadingState& progress() const noexcept { return progress_; }

    // 设置进度并做语义校验：章节号越界 / textOffset 越出该章解码文本 /
    // textOffset 不在码点边界 -> InvalidArgument；Book 已 close -> InvalidHandle。
    // pageIndex 与 textOffset 不一致时以 textOffset 为准（pageIndex 仅是派生
    // 缓存，可用 locatePage 按页区间修正/重映射）；state.pageIndex 原样记录。
    Result<void> setProgress(const Book& book, const ReadingState& state);

    // 用给定页区间（须为当前进度章节的页区间，调用方保证）把 pageIndex 对齐到
    // textOffset：换分页配置后重新 paginate 再调本函数即「按 textOffset 尽量
    // 恢复到新页」。返回对齐后的 pageIndex。
    Result<size_t> locatePage(const std::vector<PageInfo>& pages);

    // —— 书签（按加入顺序排列，下标从 0 起）——
    // 位置校验同 setProgress；成功返回新书签下标。
    Result<size_t> addBookmark(const Book& book, const Bookmark& bookmark);
    // 删除并返回被删书签；下标越界 -> NotFound（「书签不存在」）。
    Result<Bookmark> removeBookmark(size_t index);
    const std::vector<Bookmark>& bookmarks() const noexcept { return bookmarks_; }

    // —— 序列化（持久化由 JS 存储层做，原生只出/入字符串）——
    std::string serialize() const;
    // 结构非法 -> ParseError；成功后整体替换进度与书签，失败保持原样。
    Result<void> deserialize(const std::string& json);

private:
    ReadingState progress_;
    std::vector<Bookmark> bookmarks_;
};

} // namespace readex

#endif // READEX_READING_STATE_HPP
