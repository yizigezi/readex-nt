// book_registry.hpp — 受管理的 Book 句柄表（PR-3）
//
// 用途：QuickJS 绑定层的 Book 对象生命周期管理。JS 对象不持有 C++ 指针，
// 只持有句柄 id（uint32_t，0 保留为无效）；句柄表统一持有 Book 实例，
// GC finalizer / 显式 close() 时经本表释放，多本书互不串扰。
// 纯 C++、不依赖 QuickJS，可在 host 单元测试直接驱动。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_BOOK_REGISTRY_HPP
#define READEX_BOOK_REGISTRY_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

#include "readex/book.hpp"

namespace readex {

class BookRegistry {
public:
    BookRegistry() = default;
    BookRegistry(const BookRegistry&) = delete;
    BookRegistry& operator=(const BookRegistry&) = delete;

    // 登记一本书，返回句柄 id（>= 1）。id 单调递增不复用（防 ABA），
    // 极端溢出时跳过在用 id；表满（无可用 id）返回 0。
    uint32_t add(Book&& book);

    // 取书；无效/已释放句柄返回 nullptr（不抛异常）。
    Book* get(uint32_t id) noexcept;
    const Book* get(uint32_t id) const noexcept;

    // 释放（close 并移出表）；幂等，返回是否实际释放了条目。
    bool release(uint32_t id) noexcept;

    // 释放全部（Readex.closeAllBooks()）；返回释放数量。
    size_t releaseAll() noexcept;

    size_t size() const noexcept;

private:
    std::map<uint32_t, Book> books_;
    uint32_t nextId_ = 1;
};

} // namespace readex

#endif // READEX_BOOK_REGISTRY_HPP
