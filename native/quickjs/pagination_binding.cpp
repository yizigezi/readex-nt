// pagination_binding.cpp — QuickJS 分页绑定（PR-4，计划书 §四）
//
// JS API 形状（`import Readex from 'readex'`）：
//
//   Readex.paginate(content, config) -> [ { startOffset, endOffset, pageIndex } ]
//       顶层主形状（首选）：content 是**已解码的一章文本**（UTF-8 字符串），
//       偏移为该字符串内的 UTF-8 字节偏移（语义见 pagination.hpp 头注释，
//       恒在码点边界、各页无缝拼接 == 原文）。
//   book.paginateChapter(index, config) -> 同上形状（便捷方法，
//       内部 getChapter + paginate，见 book_binding.cpp）。
//
// config 字段校验策略（README 有同款说明，host 冒烟已测）：
//   - config 必须是对象（null/undefined/原始类型 -> TypeError）；
//   - width / height 必填（可用阅读区域必须由 UI 显式传入），缺失 ->
//     Error code=InvalidArgument；
//   - 其余字段缺省用默认值（fontSize=20 / lineHeight=1.5 / letterSpacing=0 /
//     paragraphSpacing=0 / 四边边距 0 / 估算系数 0.5·1.0·1.0）；
//   - 字段存在但不是数字 -> TypeError；数值非法（<=0、NaN、inf、边距吃光
//     可用区等）-> Error code=InvalidArgument（核心 validatePaginationConfig
//     统一判）；未知字段忽略。
//
// 护栏沿用 binding_util：C++ 异常绝不穿过 QuickJS C ABI。
//
// SPDX-License-Identifier: GPL-3.0

#include "quickjs.h"

#include "binding_util.hpp"

#include "readex/error.hpp"
#include "readex/pagination.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace readex_bind {
namespace {

// 读一个数字字段。返回值：1 已填入；0 字段缺省（留默认值）；-1 异常已挂起。
int get_float_field(JSContext* ctx, JSValueConst obj, const char* name,
                    float* out, bool required)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, name);
    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        JS_FreeValue(ctx, v);
        if (!required)
            return 0;
        JS_Throw(ctx,
                 make_js_error(ctx,
                               readex::Error(
                                   readex::ErrorCode::InvalidArgument,
                                   std::string("config.") + name +
                                       " 缺失（可用阅读区域等必填字段须由 UI "
                                       "显式传入）")));
        return -1;
    }
    if (!JS_IsNumber(v)) {
        JS_ThrowTypeError(ctx, "config.%s 应为数字，实际为 %s", name,
                          js_type_name(v));
        JS_FreeValue(ctx, v);
        return -1;
    }
    double d = 0;
    if (JS_ToFloat64(ctx, &d, v) < 0) {
        JS_FreeValue(ctx, v);
        return -1;
    }
    JS_FreeValue(ctx, v);
    *out = static_cast<float>(d);
    return 1;
}

} // namespace

int parse_pagination_config(JSContext* ctx, JSValueConst cfg,
                            readex::PaginationConfig* out)
{
    if (!JS_IsObject(cfg)) {
        JS_ThrowTypeError(ctx,
                          "config 应为对象（PaginationConfig），实际为 %s",
                          js_type_name(cfg));
        return -1;
    }
    *out = readex::PaginationConfig(); // 全字段默认值起步

    struct Entry {
        const char* name;
        float* target;
        bool required;
    };
    Entry entries[] = {
        { "width", &out->width, true },
        { "height", &out->height, true },
        { "fontSize", &out->fontSize, false },
        { "lineHeight", &out->lineHeight, false },
        { "letterSpacing", &out->letterSpacing, false },
        { "paragraphSpacing", &out->paragraphSpacing, false },
        { "paddingTop", &out->paddingTop, false },
        { "paddingBottom", &out->paddingBottom, false },
        { "paddingLeft", &out->paddingLeft, false },
        { "paddingRight", &out->paddingRight, false },
        // 估算模型参数（EstimatorConfig）
        { "asciiWidthEm", &out->estimator.asciiWidthEm, false },
        { "cjkWidthEm", &out->estimator.cjkWidthEm, false },
        { "punctuationWidthEm", &out->estimator.punctuationWidthEm, false },
    };
    for (const Entry& e : entries) {
        if (get_float_field(ctx, cfg, e.name, e.target, e.required) < 0)
            return -1;
    }
    return 0;
}

JSValue pages_to_js(JSContext* ctx, const std::vector<readex::PageInfo>& pages)
{
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < static_cast<uint32_t>(pages.size()); ++i) {
        const readex::PageInfo& p = pages[i];
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "startOffset",
                          JS_NewFloat64(ctx, static_cast<double>(p.startOffset)));
        JS_SetPropertyStr(ctx, obj, "endOffset",
                          JS_NewFloat64(ctx, static_cast<double>(p.endOffset)));
        JS_SetPropertyStr(ctx, obj, "pageIndex",
                          JS_NewFloat64(ctx, static_cast<double>(p.pageIndex)));
        JS_SetPropertyUint32(ctx, arr, i, obj);
    }
    return arr;
}

// ---- Readex.paginate(content, config) -> PageInfo[] ----
JSValue js_readex_paginate(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv)
{
    (void)this_val;
    READEX_GUARD_BEGIN(ctx)

    if (argc < 1)
        return JS_ThrowTypeError(
            ctx, "Readex.paginate: 需要 (content, config) 两个参数，实际收到 %d 个",
            argc);
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(
            ctx, "Readex.paginate: 第 1 个参数应为字符串（已解码章文本），实际为 %s",
            js_type_name(argv[0]));
    if (argc < 2 || !JS_IsObject(argv[1]))
        return JS_ThrowTypeError(
            ctx, "Readex.paginate: 第 2 个参数应为对象（config），实际为 %s",
            argc < 2 ? "undefined" : js_type_name(argv[1]));

    // content 可能含内嵌 NUL，用带长度的转换；偏移即此 UTF-8 串的字节偏移
    size_t len = 0;
    const char* cstr = JS_ToCStringLen(ctx, &len, argv[0]);
    if (cstr == nullptr)
        return JS_EXCEPTION;
    std::string content(cstr, len);
    JS_FreeCString(ctx, cstr);

    readex::PaginationConfig config;
    if (parse_pagination_config(ctx, argv[1], &config) < 0)
        return JS_EXCEPTION;

    readex::Result<std::vector<readex::PageInfo>> res =
        readex::PaginationEngine::paginate(content, config);
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));
    return pages_to_js(ctx, res.value());

    READEX_GUARD_END(ctx)
}

} // namespace readex_bind
