// state_binding.cpp — QuickJS 阅读状态绑定（PR-5，计划书 §四/§3.7）
//
// JS API 形状（Book 对象方法）：
//
//   book.getProgress() -> { chapterIndex, pageIndex, textOffset }
//   book.setProgress({ chapterIndex, textOffset, pageIndex? }) -> 同上（生效值）
//       chapterIndex / textOffset 必填数字（非负整数；章节号越界 / 偏移越出
//       章文本 / 不在码点边界 -> Error code=InvalidArgument）；
//       pageIndex 可选（缺省：同章沿用旧值，换章归 0）。textOffset 是定位的
//       唯一事实源，pageIndex 只是派生缓存。
//   book.locatePage(pages) -> number（pageIndex）
//       pages = PageInfo[]（形状同 book.paginateChapter 输出，pageIndex 字段
//       忽略，按数组序号）；用页区间反查当前进度 textOffset 所在页并写回。
//       换分页配置后恢复位置 = 重新 paginateChapter + locatePage。
//       空数组 / 偏移不在任何页区间 -> Error code=InvalidArgument。
//   book.addBookmark({ chapterIndex, textOffset, note?, createdAt? }) -> number（下标）
//       位置校验同 setProgress；note / createdAt 可选字符串（createdAt 是
//       自由文本，原生只存不解释）。
//   book.removeBookmark(index) -> 被删书签 { chapterIndex, textOffset, note, createdAt }
//       下标越界 -> Error code=NotFound。
//   book.listBookmarks() -> 书签数组（加入顺序）
//   book.serializeState() -> string（JSON，version 1，格式见 README）
//   book.deserializeState(json) -> undefined
//       结构非法 -> Error code=ParseError；载入前对进度与每个书签做语义复核
//       （章节/偏移），非法 -> Error code=InvalidArgument；两种失败都保持原状态。
//
// 校验口径：JS 类型不对 -> TypeError；数值非法 -> Error code=InvalidArgument。
// 护栏沿用 binding_util：C++ 异常绝不穿过 C ABI。
//
// SPDX-License-Identifier: GPL-3.0

#include "quickjs.h"

#include "binding_util.hpp"
#include "book_handle.hpp"

#include "readex/book.hpp"
#include "readex/error.hpp"
#include "readex/reading_state.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace readex_bind {
namespace {

// uint64 -> size_t（32 位设备防截断）
int check_size_range(JSContext* ctx, uint64_t v, const char* what, size_t* out)
{
    if (v > static_cast<uint64_t>(static_cast<size_t>(-1))) {
        JS_Throw(ctx, make_js_error(
                          ctx, readex::Error(readex::ErrorCode::InvalidArgument,
                                             std::string(what) +
                                                 " 超出平台 size_t 范围")));
        return -1;
    }
    *out = static_cast<size_t>(v);
    return 0;
}

JSValue progress_to_js(JSContext* ctx, const readex::ReadingState& p)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "chapterIndex",
                      JS_NewFloat64(ctx, static_cast<double>(p.chapterIndex)));
    JS_SetPropertyStr(ctx, obj, "pageIndex",
                      JS_NewFloat64(ctx, static_cast<double>(p.pageIndex)));
    JS_SetPropertyStr(ctx, obj, "textOffset",
                      JS_NewFloat64(ctx, static_cast<double>(p.textOffset)));
    return obj;
}

JSValue bookmark_to_js(JSContext* ctx, const readex::Bookmark& bm)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "chapterIndex",
                      JS_NewFloat64(ctx, static_cast<double>(bm.chapterIndex)));
    JS_SetPropertyStr(ctx, obj, "textOffset",
                      JS_NewFloat64(ctx, static_cast<double>(bm.textOffset)));
    JS_SetPropertyStr(ctx, obj, "note",
                      JS_NewStringLen(ctx, bm.note.data(), bm.note.size()));
    JS_SetPropertyStr(ctx, obj, "createdAt",
                      JS_NewStringLen(ctx, bm.createdAt.data(),
                                      bm.createdAt.size()));
    return obj;
}

} // namespace

// ---- book.getProgress() ----
JSValue js_book_get_progress(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv)
{
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    return progress_to_js(ctx, b->state().progress());
    READEX_GUARD_END(ctx)
}

// ---- book.setProgress({ chapterIndex, textOffset, pageIndex? }) ----
JSValue js_book_set_progress(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(
            ctx,
            "Book.setProgress: 第 1 个参数应为对象（{chapterIndex, textOffset, "
            "pageIndex?}），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));

    uint64_t ch = 0, off = 0, pg = 0;
    if (get_uint_field(ctx, argv[0], "chapterIndex", &ch, true) < 0)
        return JS_EXCEPTION;
    if (get_uint_field(ctx, argv[0], "textOffset", &off, true) < 0)
        return JS_EXCEPTION;
    int hasPg = get_uint_field(ctx, argv[0], "pageIndex", &pg, false);
    if (hasPg < 0)
        return JS_EXCEPTION;

    readex::ReadingState st;
    if (check_size_range(ctx, ch, "chapterIndex", &st.chapterIndex) < 0)
        return JS_EXCEPTION;
    st.textOffset = off;

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    if (hasPg == 1) {
        if (check_size_range(ctx, pg, "pageIndex", &st.pageIndex) < 0)
            return JS_EXCEPTION;
    } else {
        // 缺省：同章沿用旧 pageIndex，换章归 0（textOffset 仍是事实源，
        // 可再用 locatePage 按页区间修正）
        const readex::ReadingState& cur = b->state().progress();
        st.pageIndex =
            (cur.chapterIndex == st.chapterIndex) ? cur.pageIndex : 0;
    }

    readex::Result<void> res = b->state().setProgress(*b, st);
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    return progress_to_js(ctx, b->state().progress());
    READEX_GUARD_END(ctx)
}

// ---- book.locatePage(pages) -> pageIndex ----
JSValue js_book_locate_page(JSContext* ctx, JSValueConst this_val, int argc,
                            JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1 || JS_IsArray(ctx, argv[0]) <= 0)
        return JS_ThrowTypeError(
            ctx, "Book.locatePage: 第 1 个参数应为数组（PageInfo[]），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));

    JSValue lenv = JS_GetPropertyStr(ctx, argv[0], "length");
    if (JS_IsException(lenv))
        return JS_EXCEPTION;
    uint64_t n = 0;
    int lr = js_to_uint(ctx, lenv, "pages.length", &n);
    JS_FreeValue(ctx, lenv);
    if (lr < 0)
        return JS_EXCEPTION;

    std::vector<readex::PageInfo> pages;
    pages.reserve(n < 4096 ? static_cast<size_t>(n) : 4096);
    for (uint64_t i = 0; i < n; ++i) {
        JSValue el = JS_GetPropertyUint32(ctx, argv[0],
                                          static_cast<uint32_t>(i));
        if (JS_IsException(el))
            return JS_EXCEPTION;
        if (!JS_IsObject(el)) {
            JS_ThrowTypeError(ctx,
                              "Book.locatePage: pages[%llu] 应为对象"
                              "（{startOffset, endOffset}），实际为 %s",
                              static_cast<unsigned long long>(i),
                              js_type_name(el));
            JS_FreeValue(ctx, el);
            return JS_EXCEPTION;
        }
        readex::PageInfo pg;
        pg.pageIndex = static_cast<size_t>(i);
        uint64_t s = 0, e = 0;
        int r1 = get_uint_field(ctx, el, "startOffset", &s, true);
        if (r1 >= 0)
            r1 = get_uint_field(ctx, el, "endOffset", &e, true);
        JS_FreeValue(ctx, el);
        if (r1 < 0)
            return JS_EXCEPTION;
        pg.startOffset = s;
        pg.endOffset = e;
        pages.push_back(pg);
    }

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<size_t> res = b->state().locatePage(pages);
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    return JS_NewFloat64(ctx, static_cast<double>(res.value()));
    READEX_GUARD_END(ctx)
}

// ---- book.addBookmark({ chapterIndex, textOffset, note?, createdAt? }) ----
JSValue js_book_add_bookmark(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(
            ctx,
            "Book.addBookmark: 第 1 个参数应为对象（{chapterIndex, textOffset, "
            "note?, createdAt?}），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));

    readex::Bookmark bm;
    uint64_t ch = 0, off = 0;
    if (get_uint_field(ctx, argv[0], "chapterIndex", &ch, true) < 0)
        return JS_EXCEPTION;
    if (get_uint_field(ctx, argv[0], "textOffset", &off, true) < 0)
        return JS_EXCEPTION;
    if (check_size_range(ctx, ch, "chapterIndex", &bm.chapterIndex) < 0)
        return JS_EXCEPTION;
    bm.textOffset = off;
    if (get_string_field(ctx, argv[0], "note", &bm.note, false) < 0)
        return JS_EXCEPTION;
    if (get_string_field(ctx, argv[0], "createdAt", &bm.createdAt, false) < 0)
        return JS_EXCEPTION;

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<size_t> res = b->state().addBookmark(*b, bm);
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    return JS_NewFloat64(ctx, static_cast<double>(res.value()));
    READEX_GUARD_END(ctx)
}

// ---- book.removeBookmark(index) -> 被删书签 ----
JSValue js_book_remove_bookmark(JSContext* ctx, JSValueConst this_val, int argc,
                                JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1 || !JS_IsNumber(argv[0]))
        return JS_ThrowTypeError(
            ctx, "Book.removeBookmark: 第 1 个参数应为数字（书签下标），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));
    uint64_t idx = 0;
    if (js_to_uint(ctx, argv[0], "index", &idx) < 0)
        return JS_EXCEPTION;

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    if (idx > static_cast<uint64_t>(static_cast<size_t>(-1)))
        return JS_Throw(ctx, make_js_error(
                                 ctx, readex::Error(
                                          readex::ErrorCode::NotFound,
                                          "书签下标超范围")));
    readex::Result<readex::Bookmark> res =
        b->state().removeBookmark(static_cast<size_t>(idx));
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    return bookmark_to_js(ctx, res.value());
    READEX_GUARD_END(ctx)
}

// ---- book.listBookmarks() -> 书签数组 ----
JSValue js_book_list_bookmarks(JSContext* ctx, JSValueConst this_val, int argc,
                               JSValueConst* argv)
{
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    const std::vector<readex::Bookmark>& bms = b->state().bookmarks();
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < static_cast<uint32_t>(bms.size()); ++i)
        JS_SetPropertyUint32(ctx, arr, i, bookmark_to_js(ctx, bms[i]));
    return arr;
    READEX_GUARD_END(ctx)
}

// ---- book.serializeState() -> JSON 字符串 ----
JSValue js_book_serialize_state(JSContext* ctx, JSValueConst this_val, int argc,
                                JSValueConst* argv)
{
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    std::string json = b->state().serialize();
    return JS_NewStringLen(ctx, json.data(), json.size());
    READEX_GUARD_END(ctx)
}

// ---- book.deserializeState(json)（结构校验 + 语义复核，失败保持原状态）----
JSValue js_book_deserialize_state(JSContext* ctx, JSValueConst this_val,
                                  int argc, JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(
            ctx, "Book.deserializeState: 第 1 个参数应为字符串（JSON），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));
    size_t len = 0;
    const char* s = JS_ToCStringLen(ctx, &len, argv[0]);
    if (s == nullptr)
        return JS_EXCEPTION;
    std::string json(s, len);
    JS_FreeCString(ctx, s);

    // 先解析到临时 store（结构校验），再整体语义复核，全过才替换
    readex::ReadingStateStore tmp;
    readex::Result<void> res = tmp.deserialize(json);
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<void> v = readex::validateStateLocation(
        *b, tmp.progress().chapterIndex, tmp.progress().textOffset);
    if (!v.ok())
        return JS_Throw(ctx, make_js_error(ctx, v.error()));
    const std::vector<readex::Bookmark>& bms = tmp.bookmarks();
    for (size_t i = 0; i < bms.size(); ++i) {
        v = readex::validateStateLocation(*b, bms[i].chapterIndex,
                                          bms[i].textOffset);
        if (!v.ok())
            return JS_Throw(ctx, make_js_error(ctx, v.error()));
    }
    b->state() = std::move(tmp);
    return JS_UNDEFINED;
    READEX_GUARD_END(ctx)
}

} // namespace readex_bind
