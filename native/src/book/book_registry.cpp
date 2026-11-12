// book_registry.cpp — 受管理的 Book 句柄表实现（PR-3）
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/book_registry.hpp"

#include <utility>

namespace readex {

uint32_t BookRegistry::add(Book&& book)
{
    // id 单调递增不复用（防 ABA）；溢出时跳过在用 id
    uint32_t id = nextId_;
    for (uint64_t guard = 0; guard < 0xFFFFFFFFull; ++guard) {
        if (id != 0 && books_.find(id) == books_.end())
            break;
        ++id;
        if (id == 0)
            ++id; // 0 保留为无效句柄
    }
    if (id == 0 || books_.find(id) != books_.end())
        return 0; // 表满（实际不可达）
    books_.emplace(id, std::move(book));
    nextId_ = id + 1;
    if (nextId_ == 0)
        nextId_ = 1;
    return id;
}

Book* BookRegistry::get(uint32_t id) noexcept
{
    auto it = books_.find(id);
    return it == books_.end() ? nullptr : &it->second;
}

const Book* BookRegistry::get(uint32_t id) const noexcept
{
    auto it = books_.find(id);
    return it == books_.end() ? nullptr : &it->second;
}

bool BookRegistry::release(uint32_t id) noexcept
{
    auto it = books_.find(id);
    if (it == books_.end())
        return false;
    it->second.close();
    books_.erase(it);
    return true;
}

size_t BookRegistry::releaseAll() noexcept
{
    size_t n = 0;
    for (auto& kv : books_) {
        kv.second.close();
        ++n;
    }
    books_.clear();
    return n;
}

size_t BookRegistry::size() const noexcept
{
    return books_.size();
}

} // namespace readex
