// book_handle.hpp — JS Book 对象的句柄访问（PR-5 起 book_binding / search_binding /
// state_binding 共享）
//
// 句柄模式沿用 PR-3（book_binding.cpp）：JS 对象 opaque 只持句柄 id，Book 实例
// 统一由受管理句柄表（book_registry.hpp）持有；本头只把「取 Book*」的访问器
// 提出来给各绑定 TU 复用，不改变生命周期规则。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_BOOK_HANDLE_HPP
#define READEX_BOOK_HANDLE_HPP

#include "quickjs.h"

#include "readex/book.hpp"
#include "readex/book_registry.hpp"

#include <cstdint>

namespace readex_bind {

// 受管理句柄表（每进程一份；多本书互不串扰）
readex::BookRegistry& registry();

// JS Book 类 id（JS_NewClassID 注册，幂等）
JSClassID& bookClassIdRef();

// JS Book 对象的 opaque：只存句柄 id（0 = 已关闭/无效），不存 C++ 指针
struct BookHandle {
    uint32_t id;
};

// 取 this_val 的 Book*；非 Book 对象或句柄无效（已关闭）时抛 JS Error 并返回
// nullptr（调用方返回 JS_EXCEPTION）
readex::Book* this_book(JSContext* ctx, JSValueConst this_val);

} // namespace readex_bind

#endif // READEX_BOOK_HANDLE_HPP
