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
