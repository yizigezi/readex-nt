// bindings.cpp — QuickJS 绑定层：JS 参数 <-> C++ 核心的转换（PR-1 冒烟骨架）
//
// 职责边界（计划书 §四）：
//   - 只做参数校验/类型转换与结果包装，业务逻辑全在 native/src/（不依赖 QuickJS）
//   - C++ 异常绝不穿过 QuickJS C ABI：每个 C 入口统一 try/catch(...)，
//     经 readex::currentExceptionToError() 转成 JS Error
//   - 参数类型错误用 JS TypeError，message 写清期望类型与实际类型
//
// 冒烟 API（JS 侧 `import Readex from 'readex'`）：
//   Readex.version() -> string
//   Readex.add(a, b) -> number      参数缺失/非数字 -> throw TypeError
//   Readex.info()    -> object { version, quickjs, platform, buildMode }
//   Readex.fail()    -> 恒 throw    演示 C++ 异常 -> JS Error 的转换
// PR-3 增补（见 book_binding.cpp）：
//   Readex.openBook(path)      -> Book 对象（getInfo/getChapters/getChapter/close）
//   Readex.closeAllBooks()     -> number（释放数量）
// PR-4 增补（见 pagination_binding.cpp）：
//   Readex.paginate(content, config) -> PageInfo[]（分页主形状）
// PR-5 增补（见 search_binding.cpp / state_binding.cpp，Book 对象方法）：
//   book.search / book.getProgress / book.setProgress / book.locatePage
//   book.addBookmark / book.removeBookmark / book.listBookmarks
//   book.serializeState / book.deserializeState
//
// SPDX-License-Identifier: GPL-3.0

#include "quickjs.h"

#include "binding_util.hpp"

#include "readex/core.hpp"
#include "readex/error.hpp"

#include <string>

namespace {

using ::readex_bind::make_js_error;
using ::readex_bind::js_type_name;

// ---- Readex.version() -> string ----
JSValue js_version(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)
    return JS_NewString(ctx, readex::version());
    READEX_GUARD_END(ctx)
}

// ---- Readex.add(a, b) -> number；缺参/非数字 -> throw TypeError ----
JSValue js_add(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    (void)this_val;
    READEX_GUARD_BEGIN(ctx)

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Readex.add: 需要 2 个数字参数，实际收到 %d 个",
                                 argc);
    }
    for (int i = 0; i < 2; ++i) {
        if (!JS_IsNumber(argv[i])) {
            return JS_ThrowTypeError(ctx, "Readex.add: 第 %d 个参数应为数字，实际为 %s",
                                     i + 1, js_type_name(argv[i]));
        }
    }

    double a = 0, b = 0;
    if (JS_ToFloat64(ctx, &a, argv[0]) < 0 || JS_ToFloat64(ctx, &b, argv[1]) < 0)
        return JS_EXCEPTION; // 已有待处理异常

    return JS_NewFloat64(ctx, readex::add(a, b));

    READEX_GUARD_END(ctx)
}

// ---- Readex.info() -> { version, quickjs, platform, buildMode } ----
JSValue js_info(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    readex_native_log::write("INFO", "api.info.called", "Readex.info invoked by JS");
    (void)this_val;
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "version", JS_NewString(ctx, readex::version()));
    JS_SetPropertyStr(ctx, obj, "quickjs",
                      JS_NewString(ctx, readex::quickjsHeaderVersion()));
    JS_SetPropertyStr(ctx, obj, "platform", JS_NewString(ctx, readex::platform()));
    JS_SetPropertyStr(ctx, obj, "buildMode", JS_NewString(ctx, readex::buildMode()));
    return obj;

    READEX_GUARD_END(ctx)
}

// ---- Readex.fail() -> 恒 throw（C++ 异常 -> JS Error 演示）----
JSValue js_fail(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    READEX_GUARD_BEGIN(ctx)

    readex::fail(); // [[noreturn]]，必然抛出 C++ 异常
    return JS_UNDEFINED; // 不可达；仅为通过编译

    READEX_GUARD_END(ctx)
}

} // namespace

// 组装 default 导出对象（由 module.cpp 收进模块），外部可见、C++ 链接即可。
JSValue readex_new_api_object(JSContext* ctx)
{
    readex_native_log::write("INFO", "api.create.begin", "creating Readex exports");
    try {
        // PR-3：Book 类注册 + 原型（幂等）
        if (::readex_bind::init_book_binding(ctx) != 0)
            return JS_Throw(
                ctx, make_js_error(
                         ctx, readex::Error(readex::ErrorCode::Internal,
                                            "Book 类初始化失败")));
        JSValue api = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, api, "version",
                          JS_NewCFunction(ctx, js_version, "version", 0));
        JS_SetPropertyStr(ctx, api, "add",
                          JS_NewCFunction(ctx, js_add, "add", 2));
        JS_SetPropertyStr(ctx, api, "info",
                          JS_NewCFunction(ctx, js_info, "info", 0));
        JS_SetPropertyStr(ctx, api, "fail",
                          JS_NewCFunction(ctx, js_fail, "fail", 0));
        // PR-3：书籍与章节管理
        JS_SetPropertyStr(ctx, api, "openBook",
                          JS_NewCFunction(ctx, ::readex_bind::js_open_book,
                                          "openBook", 1));
        readex_native_log::write("INFO", "api.export.openBook", "native function installed on Readex object");
        JS_SetPropertyStr(ctx, api, "closeAllBooks",
                          JS_NewCFunction(ctx, ::readex_bind::js_close_all_books,
                                          "closeAllBooks", 0));
        // PR-4：分页引擎（顶层主形状）
        JS_SetPropertyStr(ctx, api, "paginate",
                          JS_NewCFunction(ctx, ::readex_bind::js_readex_paginate,
                                          "paginate", 2));
        return api;
    } catch (...) {
        return JS_Throw(ctx, make_js_error(ctx, readex::currentExceptionToError()));
    }
}
