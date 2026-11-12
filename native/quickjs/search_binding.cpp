// search_binding.cpp — QuickJS 全文搜索绑定（PR-5，计划书 §四）
//
// JS API 形状（`import Readex from 'readex'`，Book 对象方法）：
//
//   book.search(keyword, limit?, options?) -> [ { chapterIndex, offset, context } ]
//       keyword   字符串（必填；空串 -> Error code=InvalidArgument）
//       limit     可选数字，正整数限流上限（缺省 100；<=0/非整数 ->
//                 Error code=InvalidArgument）
//       options   可选对象 { contextBefore?, contextAfter? }（前后文摘录的
//                 字节预算，缺省各 32；非负整数；未知字段忽略）
//
// offset 是**章内已解码文本（UTF-8）的字节偏移**（与 PageInfo /
// ReadingState.textOffset 同一把尺子，见 search.hpp 头注释）。
// 校验口径与分页绑定一致：JS 类型不对 -> TypeError；数值非法 ->
// Error code=InvalidArgument。护栏沿用 binding_util：C++ 异常绝不穿过 C ABI。
//
// SPDX-License-Identifier: GPL-3.0

#include "quickjs.h"

#include "binding_util.hpp"
#include "book_handle.hpp"

#include "readex/error.hpp"
#include "readex/search.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace readex_bind {
namespace {

// 读 options 的 context 预算字段（缺省用 SearchOptions 默认值）
int parse_search_options(JSContext* ctx, JSValueConst opts,
                         readex::SearchOptions* out)
{
    if (!JS_IsObject(opts)) {
        JS_ThrowTypeError(ctx,
                          "Book.search: 第 3 个参数应为对象（options），实际为 %s",
                          js_type_name(opts));
        return -1;
    }
    uint64_t v = 0;
    int r = get_uint_field(ctx, opts, "contextBefore", &v, false);
    if (r < 0)
        return -1;
    if (r == 1)
        out->contextBefore = static_cast<size_t>(v);
    r = get_uint_field(ctx, opts, "contextAfter", &v, false);
    if (r < 0)
        return -1;
    if (r == 1)
        out->contextAfter = static_cast<size_t>(v);
    return 0;
}

} // namespace

// ---- book.search(keyword, limit?, options?) -> SearchResult[] ----
JSValue js_book_search(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv)
{
    READEX_GUARD_BEGIN(ctx)

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(
            ctx, "Book.search: 第 1 个参数应为字符串（关键词），实际为 %s",
            argc < 1 ? "undefined" : js_type_name(argv[0]));
    size_t klen = 0;
    const char* kcstr = JS_ToCStringLen(ctx, &klen, argv[0]);
    if (kcstr == nullptr)
        return JS_EXCEPTION;
    std::string keyword(kcstr, klen); // 关键词可含任意字节，按字节子串匹配
    JS_FreeCString(ctx, kcstr);

    uint64_t limit = 100; // 缺省限流上限
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        if (js_to_uint(ctx, argv[1], "limit", &limit) < 0)
            return JS_EXCEPTION;
    }
    if (limit == 0 || limit > static_cast<uint64_t>(static_cast<size_t>(-1)))
        return JS_Throw(
            ctx, make_js_error(
                     ctx, readex::Error(readex::ErrorCode::InvalidArgument,
                                        "Book.search: limit 须为正数且不超平台范围")));

    readex::SearchOptions opts;
    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
        if (parse_search_options(ctx, argv[2], &opts) < 0)
            return JS_EXCEPTION;
    }

    readex::Book* b = this_book(ctx, this_val);
    if (b == nullptr)
        return JS_EXCEPTION;
    readex::Result<std::vector<readex::SearchResult>> res =
        readex::SearchEngine::search(*b, keyword, static_cast<size_t>(limit),
                                     opts);
    if (!res.ok())
        return JS_Throw(ctx, make_js_error(ctx, res.error()));

    JSValue arr = JS_NewArray(ctx);
    const std::vector<readex::SearchResult>& rs = res.value();
    for (uint32_t i = 0; i < static_cast<uint32_t>(rs.size()); ++i) {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "chapterIndex",
                          JS_NewFloat64(ctx, static_cast<double>(rs[i].chapterIndex)));
        JS_SetPropertyStr(ctx, obj, "offset",
                          JS_NewFloat64(ctx, static_cast<double>(rs[i].offset)));
        JS_SetPropertyStr(ctx, obj, "context",
                          JS_NewStringLen(ctx, rs[i].context.data(),
                                          rs[i].context.size()));
        JS_SetPropertyUint32(ctx, arr, i, obj);
    }
    return arr;

    READEX_GUARD_END(ctx)
}

} // namespace readex_bind
