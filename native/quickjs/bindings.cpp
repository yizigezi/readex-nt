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
//
// SPDX-License-Identifier: GPL-3.0

#include "quickjs.h"

#include "readex/core.hpp"
#include "readex/error.hpp"

#include <string>

namespace {

// JS 值类型名（用于参数错误信息）
const char* js_type_name(JSValueConst v)
{
    switch (JS_VALUE_GET_NORM_TAG(v)) {
    case JS_TAG_BIG_INT: return "bigint";
    case JS_TAG_BIG_FLOAT: return "bigfloat";
    case JS_TAG_BIG_DECIMAL: return "bigdecimal";
    case JS_TAG_SYMBOL: return "symbol";
    case JS_TAG_STRING: return "string";
    case JS_TAG_OBJECT: return "object";
    case JS_TAG_INT:
    case JS_TAG_FLOAT64: return "number";
    case JS_TAG_BOOL: return "boolean";
    case JS_TAG_NULL: return "null";
    case JS_TAG_UNDEFINED: return "undefined";
    case JS_TAG_UNINITIALIZED: return "uninitialized";
    case JS_TAG_EXCEPTION: return "exception";
    default: return "unknown";
    }
}

// readex::Error -> JS Error 对象（携带 message 与 code 属性）
JSValue make_js_error(JSContext* ctx, const readex::Error& e)
{
    std::string msg = e.toString();
    JSValue err = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, msg.c_str()));
    JS_SetPropertyStr(ctx, err, "code",
                      JS_NewString(ctx, readex::errorCodeName(e.code())));
    return err;
}

// —— ABI 边界护栏：异常一律转 JS Error，绝不越过 C ABI ——
#define READEX_GUARD_BEGIN(ctx) \
    try {
#define READEX_GUARD_END(ctx)                                      \
    } catch (...) {                                                \
        return JS_Throw((ctx),                                     \
                        make_js_error((ctx),                       \
                                      ::readex::currentExceptionToError())); \
    }

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
    try {
        JSValue api = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, api, "version",
                          JS_NewCFunction(ctx, js_version, "version", 0));
        JS_SetPropertyStr(ctx, api, "add",
                          JS_NewCFunction(ctx, js_add, "add", 2));
        JS_SetPropertyStr(ctx, api, "info",
                          JS_NewCFunction(ctx, js_info, "info", 0));
        JS_SetPropertyStr(ctx, api, "fail",
                          JS_NewCFunction(ctx, js_fail, "fail", 0));
        return api;
    } catch (...) {
        return JS_Throw(ctx, make_js_error(ctx, readex::currentExceptionToError()));
    }
}
