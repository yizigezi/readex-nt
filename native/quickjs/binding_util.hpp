// binding_util.hpp — QuickJS 绑定层公共工具（PR-3 起 bindings.cpp 与
// book_binding.cpp 共享）
//
// 职责边界（计划书 §四）：
//   - 只做参数校验/类型转换与结果包装，业务逻辑全在 native/src/（不依赖 QuickJS）
//   - C++ 异常绝不穿过 QuickJS C ABI：每个 C 入口统一 READEX_GUARD_*，
//     经 readex::currentExceptionToError() 转成 JS Error
//   - 参数类型错误用 JS TypeError，message 写清期望类型与实际类型
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_BINDING_UTIL_HPP
#define READEX_BINDING_UTIL_HPP

#include "quickjs.h"

#include "readex/error.hpp"
#include "readex/pagination.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace readex_bind {

// JS 值类型名（用于参数错误信息）
inline const char* js_type_name(JSValueConst v)
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
inline JSValue make_js_error(JSContext* ctx, const readex::Error& e)
{
    std::string msg = e.toString();
    JSValue err = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, msg.c_str()));
    JS_SetPropertyStr(ctx, err, "code",
                      JS_NewString(ctx, readex::errorCodeName(e.code())));
    return err;
}

// ---- Book 绑定（book_binding.cpp）----
// 初始化 Book 类与原型（幂等，可重复调用）；openBook / closeAllBooks 挂进
// Readex API 对象。
int init_book_binding(JSContext* ctx);
JSValue js_open_book(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv);
JSValue js_close_all_books(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv);

// ---- 分页绑定（pagination_binding.cpp，PR-4）----
// 解析 JS config 对象 -> PaginationConfig。返回 0 成功；-1 时 JS 异常已挂起。
// 校验策略（README 同款）：config 必须是对象（否则 TypeError）；width/height
// 必填（缺失 -> Error code=InvalidArgument）；其余字段缺省用默认值；字段类型
// 不是数字 -> TypeError；数值合法性由核心 validatePaginationConfig 统一判
// （-> Error code=InvalidArgument）；未知字段忽略。
int parse_pagination_config(JSContext* ctx, JSValueConst cfg,
                            readex::PaginationConfig* out);
// 把分页结果包装成 JS 数组 [{startOffset, endOffset, pageIndex}]
JSValue pages_to_js(JSContext* ctx, const std::vector<readex::PageInfo>& pages);
// Readex.paginate(content, config) -> PageInfo[]（顶层主形状）
JSValue js_readex_paginate(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv);

// ---- PR-5 数值/字符串参数工具（search_binding / state_binding 共用）----
// 校验口径（README 同款）：JS 类型不对 -> TypeError；数值非整数/负数/超范围
// -> Error code=InvalidArgument；不崩溃、不静默截断。

// JS 数值 -> 非负整数（uint64）。返回 0 成功；-1 异常已挂起。
// 校验：非负、有限、整数、< 2^64（NaN/inf/小数/负数/超范围 -> InvalidArgument）。
inline int js_to_uint(JSContext* ctx, JSValueConst v, const char* what,
                      uint64_t* out)
{
    if (!JS_IsNumber(v)) {
        JS_ThrowTypeError(ctx, "%s 应为数字，实际为 %s", what,
                          js_type_name(v));
        return -1;
    }
    double d = 0;
    if (JS_ToFloat64(ctx, &d, v) < 0)
        return -1;
    // 2^64 可被 double 精确表示，用 < 2^64 判上界；floor 判整数（NaN 先被
    // d >= 0 短路掉）
    if (!(d >= 0.0) || !(d < 18446744073709551616.0) ||
        d != std::floor(d)) {
        JS_Throw(ctx, ::readex_bind::make_js_error(
                          ctx, ::readex::Error(
                                   ::readex::ErrorCode::InvalidArgument,
                                   std::string(what) +
                                       " 须为非负整数且不超 64 位范围")));
        return -1;
    }
    *out = static_cast<uint64_t>(d);
    return 0;
}

// 读对象的非负整数字段。返回 1 已填入；0 字段缺省；-1 异常已挂起。
inline int get_uint_field(JSContext* ctx, JSValueConst obj, const char* name,
                          uint64_t* out, bool required)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, name);
    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        JS_FreeValue(ctx, v);
        if (!required)
            return 0;
        JS_Throw(ctx, ::readex_bind::make_js_error(
                          ctx, ::readex::Error(
                                   ::readex::ErrorCode::InvalidArgument,
                                   std::string(name) + " 缺失（必填字段）")));
        return -1;
    }
    if (js_to_uint(ctx, v, name, out) < 0) {
        JS_FreeValue(ctx, v);
        return -1;
    }
    JS_FreeValue(ctx, v);
    return 1; // 已填入（js_to_uint 成功返回 0，此处转换为契约值 1）
}

// 读对象的字符串字段。返回 1 已填入；0 字段缺省；-1 异常已挂起。
inline int get_string_field(JSContext* ctx, JSValueConst obj, const char* name,
                            std::string* out, bool required)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, name);
    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        JS_FreeValue(ctx, v);
        if (!required)
            return 0;
        JS_Throw(ctx, ::readex_bind::make_js_error(
                          ctx, ::readex::Error(
                                   ::readex::ErrorCode::InvalidArgument,
                                   std::string(name) + " 缺失（必填字段）")));
        return -1;
    }
    if (!JS_IsString(v)) {
        JS_ThrowTypeError(ctx, "%s 应为字符串，实际为 %s", name,
                          js_type_name(v));
        JS_FreeValue(ctx, v);
        return -1;
    }
    size_t len = 0;
    const char* s = JS_ToCStringLen(ctx, &len, v);
    JS_FreeValue(ctx, v);
    if (s == nullptr)
        return -1;
    out->assign(s, len);
    JS_FreeCString(ctx, s);
    return 1;
}

// ---- 搜索绑定（search_binding.cpp，PR-5）----
JSValue js_book_search(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv);

// ---- 阅读状态绑定（state_binding.cpp，PR-5）----
JSValue js_book_get_progress(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv);
JSValue js_book_set_progress(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv);
JSValue js_book_locate_page(JSContext* ctx, JSValueConst this_val, int argc,
                            JSValueConst* argv);
JSValue js_book_add_bookmark(JSContext* ctx, JSValueConst this_val, int argc,
                             JSValueConst* argv);
JSValue js_book_remove_bookmark(JSContext* ctx, JSValueConst this_val, int argc,
                                JSValueConst* argv);
JSValue js_book_list_bookmarks(JSContext* ctx, JSValueConst this_val, int argc,
                               JSValueConst* argv);
JSValue js_book_serialize_state(JSContext* ctx, JSValueConst this_val, int argc,
                                JSValueConst* argv);
JSValue js_book_deserialize_state(JSContext* ctx, JSValueConst this_val,
                                  int argc, JSValueConst* argv);

} // namespace readex_bind

// —— ABI 边界护栏：异常一律转 JS Error，绝不越过 C ABI ——
#define READEX_GUARD_BEGIN(ctx) \
    try {
#define READEX_GUARD_END(ctx)                                      \
    } catch (...) {                                                \
        return JS_Throw((ctx),                                     \
                        ::readex_bind::make_js_error(              \
                            (ctx), ::readex::currentExceptionToError())); \
    }

#endif // READEX_BINDING_UTIL_HPP
