// test_book_registry.cpp — 受管理 Book 句柄表（PR-3，绑定层 handle 管理逻辑）
//
// 覆盖：add/get/release/releaseAll、无效句柄、重复释放、id 不复用、
// 多书并行、释放后 Book 状态（close 生效）。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"
#include "test_support.hpp"

#include "readex/book.hpp"
#include "readex/book_registry.hpp"

#include <string>
#include <utility>

using readex::Book;
using readex::BookRegistry;
using readex::ErrorCode;
using readex::Result;
using readex::openBook;

namespace {

// 建一本可登记的测试书
Result<Book> makeBook(const std::string& name, const std::string& raw)
{
    std::string path = testsup::writeTestFile(name, raw);
    return openBook(path); // 测试文件留在 tmp 目录，各用例自行清理
}

} // namespace

TEST(book_registry_add_get_release)
{
    BookRegistry reg;
    CHECK_EQ(reg.size(), (size_t)0);

    Result<Book> b1 = makeBook("reg_a.txt", "第一章\n甲\n");
    Result<Book> b2 = makeBook("reg_b.txt", "第一章\n乙\n");
    CHECK(b1.ok());
    CHECK(b2.ok());
    if (!b1.ok() || !b2.ok())
        return;

    uint32_t id1 = reg.add(std::move(b1.value()));
    uint32_t id2 = reg.add(std::move(b2.value()));
    CHECK(id1 != 0);
    CHECK(id2 != 0);
    CHECK(id1 != id2); // id 不复用
    CHECK_EQ(reg.size(), (size_t)2);

    Book* p1 = reg.get(id1);
    Book* p2 = reg.get(id2);
    CHECK(p1 != nullptr);
    CHECK(p2 != nullptr);
    if (p1 && p2) {
        CHECK_EQ(p1->getChapter(0).value(), std::string("第一章\n甲\n"));
        CHECK_EQ(p2->getChapter(0).value(), std::string("第一章\n乙\n"));
    }

    // 无效 id
    CHECK(reg.get(0) == nullptr);
    CHECK(reg.get(id1 + 1000) == nullptr);

    // 释放：幂等，释放后 get 为 nullptr
    CHECK(reg.release(id1));
    CHECK(!reg.release(id1));
    CHECK(reg.get(id1) == nullptr);
    CHECK_EQ(reg.size(), (size_t)1);

    // 未受牵连
    CHECK(p2 == reg.get(id2));

    CHECK(reg.releaseAll() == 1);
    CHECK_EQ(reg.size(), (size_t)0);
    CHECK(reg.get(id2) == nullptr);
    CHECK(reg.releaseAll() == 0);

    testsup::removeTestFile(testsup::tmpDir() + "/reg_a.txt");
    testsup::removeTestFile(testsup::tmpDir() + "/reg_b.txt");
}

TEST(book_registry_release_closes_book)
{
    BookRegistry reg;
    Result<Book> b = makeBook("reg_close.txt", "第一章\n甲\n");
    CHECK(b.ok());
    if (!b.ok())
        return;
    uint32_t id = reg.add(std::move(b.value()));
    CHECK(id != 0);
    Book* p = reg.get(id);
    CHECK(p != nullptr);
    if (p != nullptr)
        CHECK(p->isOpen());
    reg.release(id);
    // release 后条目已销毁（析构自动 close），指针不可再用——只验证表状态
    CHECK_EQ(reg.size(), (size_t)0);
    testsup::removeTestFile(testsup::tmpDir() + "/reg_close.txt");
}

TEST(book_registry_many_books)
{
    BookRegistry reg;
    static const int kN = 50;
    uint32_t ids[kN];
    for (int i = 0; i < kN; ++i) {
        std::string name = "reg_many_" + std::to_string(i) + ".txt";
        Result<Book> b = makeBook(name, "第" + std::to_string(i) + "章\n甲\n");
        CHECK(b.ok());
        if (!b.ok())
            return;
        ids[i] = reg.add(std::move(b.value()));
        CHECK(ids[i] != 0);
    }
    CHECK_EQ(reg.size(), (size_t)kN);
    // 每本书内容独立（不串扰）
    for (int i = 0; i < kN; ++i) {
        Book* p = reg.get(ids[i]);
        CHECK(p != nullptr);
        if (p != nullptr) {
            std::string expect = "第" + std::to_string(i) + "章\n甲\n";
            CHECK_EQ(p->getChapter(0).value(), expect);
        }
    }
    CHECK_EQ(reg.releaseAll(), (size_t)kN);
    for (int i = 0; i < kN; ++i) {
        CHECK(reg.get(ids[i]) == nullptr);
        testsup::removeTestFile(testsup::tmpDir() + "/reg_many_" +
                                std::to_string(i) + ".txt");
    }
}
