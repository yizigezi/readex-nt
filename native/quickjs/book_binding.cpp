// book_binding.cpp — QuickJS Book 对象绑定（PR-3，计划书 §四）
//
// JS API 形状（宿主 QuickJS 头 2020-07-05 提供 JS_NewClass/JS_SetOpaque 等
// 对象生命周期 API，编译可用，故采用「JS Book 对象持有句柄」方案，不退化为
// 裸 handle id + 顶层方法；README 有同样说明）：
//
//   const book = Readex.openBook(path)   -> Book 对象（类 "Book"）
//   book.getInfo()      -> { title, path, sizeBytes, encoding, chapterCount }
//   book.getChapters()  -> [ { title, index, startOffset, endOffset } ]
//   book.getChapter(i)  -> { title, index, text }   （text 为 UTF-8 字符串）
//   book.paginateChapter(i, config) -> PageInfo[]   （PR-4 便捷：getChapter +
//       Readex.paginate，形状与顶层一致；主形状是 Readex.paginate）
//   book.search(keyword, limit?, options?) -> SearchResult[]（PR-5，见
//       search_binding.cpp）
//   book.getProgress() / book.setProgress({chapterIndex, textOffset, pageIndex?})
//   book.locatePage(pages) / book.addBookmark(...) / book.removeBookmark(i)
//   book.listBookmarks() / book.serializeState() / book.deserializeState(json)
//       （PR-5 阅读状态，见 state_binding.cpp）
//   book.close()        -> undefined（幂等）
//   Readex.closeAllBooks() -> number（关闭并释放的书数量）
//
// 生命周期：
//   - JS 对象不暴露 C++ 指针：opaque 只持句柄 id（BookHandle），Book 实例
//     统一由受管理句柄表（book_registry.hpp）持有；
//   - 显式 close() 或对象被 GC（class finalizer）都会释放句柄表条目；
//     close 后对象仍在，但业务方法报 InvalidHandle 错误，重复 close 安全；
//   - 参数校验 + C++ 异常 -> JS Error 护栏沿用 bindings.cpp 现有模式。
//
// SPDX-License-Identifier: GPL-3.0

#include "quickjs.h"

#include "binding_util.hpp"
#include "book_handle.hpp"

#include "readex/book.hpp"
#include "readex/book_registry.hpp"
#include "readex/decoder.hpp"
#include "readex/error.hpp"
#include "readex/pagination.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace readex_bind {

// ---- 句柄访问器（book_handle.hpp 声明，供各绑定 TU 复用）----
readex::BookRegistry& registry()
{
    static readex::BookRegistry r;
    return r;
}

JSClassID& bookClassIdRef()
{
    static JSClassID id = 0;
    return id;
}

// 取 this_val 的 Book*；非 Book 对象或句柄无效（已关闭）时抛 JS Error 并
// 返回 nullptr（调用方返回 JS_EXCEPTION）
readex::Book* this_book(JSContext* ctx, JSValueConst this_val)
{
    BookHandle* h = static_cast<BookHandle*>(
        JS_GetOpaque2(ctx, this_val, bookClassIdRef()));
    if (h == nullptr)
        return nullptr; // JS_GetOpaque2 已抛 TypeError
    if (h->id == 0) {
        JS_Throw(ctx, make_js_error(
                          ctx, readex::Error(readex::ErrorCode::InvalidHandle,
                                             "Book: 对象已关闭")));
        return nullptr;
    }
    readex::Book* b = registry().get(h->id);
    if (b == nullptr) {
        JS_Throw(ctx, make_js_error(
                          ctx, readex::Error(readex::ErrorCode::InvalidHandle,
                                             "Book: 句柄已失效")));
        return nullptr;
    }
    return b;
}

namespace {

void book_finalizer(JSRuntime* rt, JSValue val)
{
    (void)rt;
    try {
        BookHandle* h =
            static_cast<BookHandle*>(JS_GetOpaque(val, bookClassIdRef()));
        if (h != nullptr) {
            if (h->id != 0)
                registry().release(h->id);
            delete h;
        }
    } catch (...) {
        // finalizer 在 GC 路径上，绝不让异常穿过 C ABI
    }
}

} // namespace

// ---- book.getInfo() -> { title, path, sizeBytes, encoding, chapterCount } ----
JSValue js_book_get_info(JSContext* ctx, JSValueConst this_val, int argc,
                         JSValueConst* argv)
{
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<readex::BookInfo> res = b->getInfo();
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    const readex::BookInfo& info = res.value();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "title",
                      JS_NewString(ctx, info.title.c_str()));
    JS_SetPropertyStr(ctx, obj, "path", JS_NewString(ctx, info.path.c_str()));
    JS_SetPropertyStr(ctx, obj, "sizeBytes",
                      JS_NewFloat64(ctx, static_cast<double>(info.sizeBytes)));
    JS_SetPropertyStr(ctx, obj, "encoding",
                      JS_NewString(ctx, readex::encodingName(info.encoding)));
    JS_SetPropertyStr(ctx, obj, "chapterCount",
                      JS_NewFloat64(ctx, static_cast<double>(info.chapterCount)));
    return obj;
    READEX_GUARD_END(ctx)
}

// ---- book.getChapters() -> [ { title, index, startOffset, endOffset } ] ----
JSValue js_book_get_chapters(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv)
{
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<std::vector<readex::ChapterInfo>> res = b->getChapters();
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    const std::vector<readex::ChapterInfo>& chapters = res.value();
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < static_cast<uint32_t>(chapters.size()); ++i) {
        const readex::ChapterInfo& ch = chapters[i];
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "title",
                          JS_NewString(ctx, ch.title.c_str()));
        JS_SetPropertyStr(ctx, obj, "index",
                          JS_NewFloat64(ctx, static_cast<double>(ch.index)));
        JS_SetPropertyStr(ctx, obj, "startOffset",
                          JS_NewFloat64(ctx, static_cast<double>(ch.startOffset)));
        JS_SetPropertyStr(ctx, obj, "endOffset",
                          JS_NewFloat64(ctx, static_cast<double>(ch.endOffset)));
        JS_SetPropertyUint32(ctx, arr, i, obj);
    }
    return arr;
    READEX_GUARD_END(ctx)
}

// ---- book.getChapter(index) -> { title, index, text } ----
JSValue js_book_get_chapter(JSContext* ctx, JSValueConst this_val, int argc,
                            JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1 || !JS_IsNumber(argv[0]))
        return JS_ThrowTypeError(
            ctx, "Book.getChapter: 第 1 个参数应为数字（章节索引），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));
    int64_t idx = -1;
    if (JS_ToInt64(ctx, &idx, argv[0]) < 0)
        return JS_EXCEPTION;
    if (idx < 0)
        return JS_ThrowRangeError(ctx, "Book.getChapter: 章节索引不能为负数: %lld",
                                  static_cast<long long>(idx));

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<std::string> res = b->getChapter(static_cast<size_t>(idx));
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));

    // 附带标题与索引，便于 UI 直接使用
    std::string title;
    readex::Result<std::vector<readex::ChapterInfo>> chres = b->getChapters();
    if (chres.ok() && static_cast<size_t>(idx) < chres.value().size())
        title = chres.value()[static_cast<size_t>(idx)].title;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "title", JS_NewString(ctx, title.c_str()));
    JS_SetPropertyStr(ctx, obj, "index",
                      JS_NewFloat64(ctx, static_cast<double>(idx)));
    // text 可能含内嵌 NUL（非法输入的解码产物），必须用 NewStringLen
    JS_SetPropertyStr(ctx, obj, "text",
                      JS_NewStringLen(ctx, res.value().data(),
                                      res.value().size()));
    return obj;
    READEX_GUARD_END(ctx)
}

// ---- book.paginateChapter(index, config) -> PageInfo[]（PR-4 便捷）----
// 内部 = getChapter(index) + PaginationEngine::paginate；偏移指向该章
// 解码文本（见 pagination.hpp 头注释的偏移语义）。
JSValue js_book_paginate_chapter(JSContext* ctx, JSValueConst this_val, int argc,
                                 JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1 || !JS_IsNumber(argv[0]))
        return JS_ThrowTypeError(
            ctx,
            "Book.paginateChapter: 第 1 个参数应为数字（章节索引），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));
    int64_t idx = -1;
    if (JS_ToInt64(ctx, &idx, argv[0]) < 0)
        return JS_EXCEPTION;
    if (idx < 0)
        return JS_ThrowRangeError(
            ctx, "Book.paginateChapter: 章节索引不能为负数: %lld",
            static_cast<long long>(idx));
    if (argc < 2 || !JS_IsObject(argv[1]))
        return JS_ThrowTypeError(
            ctx, "Book.paginateChapter: 第 2 个参数应为对象（config），实际为 %s",
            argc < 2 ? "undefined" : js_type_name(argv[1]));

    readex::PaginationConfig config;
    if (parse_pagination_config(ctx, argv[1], &config) < 0)
        return JS_EXCEPTION;

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<std::string> res = b->getChapter(static_cast<size_t>(idx));
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));

    readex::Result<std::vector<readex::PageInfo>> pages =
        readex::PaginationEngine::paginate(res.value(), config);
    if (!pages.ok())
        return JS_Throw(ctx, make_js_error(ctx, pages.error()));
    return pages_to_js(ctx, pages.value());
    READEX_GUARD_END(ctx)
}

// ---- book.close()（幂等）----
JSValue js_book_close(JSContext* ctx, JSValueConst this_val, int argc,
                      JSValueConst* argv)
{
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    BookHandle* h = static_cast<BookHandle*>(
        JS_GetOpaque2(ctx, this_val, bookClassIdRef()));
    if (h == nullptr)
        return JS_EXCEPTION; // 非 Book 对象 -> TypeError
    if (h->id != 0) {
        registry().release(h->id);
        h->id = 0;
    }
    return JS_UNDEFINED;
    READEX_GUARD_END(ctx)
}

namespace {

// 建 Book 类（每 runtime 一次）+ 原型（每 context）；幂等
int init_book_binding_impl(JSContext* ctx)
{
    JSClassID& id = bookClassIdRef();
    if (id == 0)
        JS_NewClassID(&id);
    JSRuntime* rt = JS_GetRuntime(ctx);
    if (!JS_IsRegisteredClass(rt, id)) {
        JSClassDef def = { "Book", book_finalizer, nullptr, nullptr, nullptr };
        if (JS_NewClass(rt, id, &def) < 0)
            return -1;
    }

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "getInfo",
                      JS_NewCFunction(ctx, js_book_get_info, "getInfo", 0));
    JS_SetPropertyStr(ctx, proto, "getChapters",
                      JS_NewCFunction(ctx, js_book_get_chapters, "getChapters", 0));
    JS_SetPropertyStr(ctx, proto, "getChapter",
                      JS_NewCFunction(ctx, js_book_get_chapter, "getChapter", 1));
    JS_SetPropertyStr(ctx, proto, "paginateChapter",
                      JS_NewCFunction(ctx, js_book_paginate_chapter,
                                      "paginateChapter", 2));
    // PR-5：全文搜索（search_binding.cpp）
    JS_SetPropertyStr(ctx, proto, "search",
                      JS_NewCFunction(ctx, js_book_search, "search", 3));
    // PR-5：阅读状态 / 书签 / 序列化（state_binding.cpp）
    JS_SetPropertyStr(ctx, proto, "getProgress",
                      JS_NewCFunction(ctx, js_book_get_progress,
                                      "getProgress", 0));
    JS_SetPropertyStr(ctx, proto, "setProgress",
                      JS_NewCFunction(ctx, js_book_set_progress,
                                      "setProgress", 1));
    JS_SetPropertyStr(ctx, proto, "locatePage",
                      JS_NewCFunction(ctx, js_book_locate_page,
                                      "locatePage", 1));
    JS_SetPropertyStr(ctx, proto, "addBookmark",
                      JS_NewCFunction(ctx, js_book_add_bookmark,
                                      "addBookmark", 1));
    JS_SetPropertyStr(ctx, proto, "removeBookmark",
                      JS_NewCFunction(ctx, js_book_remove_bookmark,
                                      "removeBookmark", 1));
    JS_SetPropertyStr(ctx, proto, "listBookmarks",
                      JS_NewCFunction(ctx, js_book_list_bookmarks,
                                      "listBookmarks", 0));
    JS_SetPropertyStr(ctx, proto, "serializeState",
                      JS_NewCFunction(ctx, js_book_serialize_state,
                                      "serializeState", 0));
    JS_SetPropertyStr(ctx, proto, "deserializeState",
                      JS_NewCFunction(ctx, js_book_deserialize_state,
                                      "deserializeState", 1));
    JS_SetPropertyStr(ctx, proto, "close",
                      JS_NewCFunction(ctx, js_book_close, "close", 0));
    JS_SetClassProto(ctx, id, proto); // 接管 proto 引用
    return 0;
}

} // namespace

int init_book_binding(JSContext* ctx)
{
    try {
        return init_book_binding_impl(ctx);
    } catch (...) {
        return -1; // 异常不越界；错误由调用方转 JS Error
    }
}

// ---- Readex.openBook(path) -> Book 对象 ----
JSValue js_open_book(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv)
{
    (void)this_val;
    READEX_GUARD_BEGIN(ctx)
    if (argc < 1)
        return JS_ThrowTypeError(ctx,
                                 "Readex.openBook: 需要 1 个字符串参数（路径），"
                                 "实际收到 %d 个",
                                 argc);
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx,
                                 "Readex.openBook: 第 1 个参数应为字符串（路径），"
                                 "实际为 %s",
                                 js_type_name(argv[0]));

    const char* path_cstr = JS_ToCString(ctx, argv[0]);
    if (path_cstr == nullptr)
        return JS_EXCEPTION; // 已有待处理异常
    std::string path(path_cstr);
    JS_FreeCString(ctx, path_cstr);

    BookHandle* h = new BookHandle{ 0 }; // bad_alloc 走护栏，无资源可泄

    readex::Result<readex::Book> res = readex::openBook(path);
    if (!res.ok()) {
        delete h;
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    }
    uint32_t id = registry().add(std::move(res.value()));
    if (id == 0) {
        delete h;
        return JS_Throw(ctx,
                        make_js_error(ctx,
                                      readex::Error(readex::ErrorCode::OutOfMemory,
                                                    "openBook: 句柄表已满")));
    }
    h->id = id;

    JSValue obj = JS_NewObjectClass(ctx, static_cast<int>(bookClassIdRef()));
    if (JS_IsException(obj)) {
        registry().release(id);
        delete h;
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, h);
    return obj;
    READEX_GUARD_END(ctx)
}

// ---- Readex.closeAllBooks() -> number（释放数量）----
JSValue js_close_all_books(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    return JS_NewFloat64(ctx, static_cast<double>(registry().releaseAll()));
    READEX_GUARD_END(ctx)
}

} // namespace readex_bind
