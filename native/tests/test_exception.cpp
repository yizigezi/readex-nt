// test_exception.cpp — 异常不跨 ABI：currentExceptionToError 转换函数
//
// 覆盖 binding 层护栏的语义：任何 C++ 异常在 catch(...) 里被转成 Error，
// 不再向外传播（即不会穿过 QuickJS C ABI）。
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"

#include "readex/error.hpp"

#include <stdexcept>
#include <string>

using readex::currentExceptionToError;
using readex::Error;
using readex::ErrorCode;

namespace {

// 模拟 binding 层 C 入口的护栏结构：抛异常进，Error 出，异常不外泄
Error guarded_call(void (*fn)())
{
    try {
        fn();
    } catch (...) {
        return currentExceptionToError();
    }
    return Error();
}

void throws_runtime_error() { throw std::runtime_error("boom"); }
void throws_logic_error() { throw std::logic_error("契约违背"); }
void throws_int() { throw 42; }

} // namespace

TEST(exception_std_exception_converts_to_internal_error)
{
    Error e = guarded_call(throws_runtime_error);
    CHECK(!e.ok());
    CHECK(e.code() == ErrorCode::Internal);
    CHECK_EQ(e.message(), std::string("C++ 异常: boom"));
}

TEST(exception_other_std_exception)
{
    Error e = guarded_call(throws_logic_error);
    CHECK(e.code() == ErrorCode::Internal);
    CHECK_EQ(e.message(), std::string("C++ 异常: 契约违背"));
}

TEST(exception_unknown_type_converts_too)
{
    Error e = guarded_call(throws_int);
    CHECK(e.code() == ErrorCode::Internal);
    CHECK_EQ(e.message(), std::string("C++ 异常: 未知类型"));
}

TEST(exception_does_not_escape_guard)
{
    // guarded_call 不应把异常继续抛出来
    bool reached = true;
    (void)guarded_call(throws_runtime_error);
    CHECK(reached);
}

TEST(exception_error_to_string_for_js_message)
{
    Error e = guarded_call(throws_runtime_error);
    CHECK_EQ(e.toString(), std::string("Internal: C++ 异常: boom"));
}
