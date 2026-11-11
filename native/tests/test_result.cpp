// test_result.cpp — Result<T> 成功/失败/移动/拷贝语义 + Result<void>
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"

#include "readex/error.hpp"

#include <string>
#include <utility>

using readex::Error;
using readex::ErrorCode;
using readex::Result;

namespace {

// 追踪对象生命周期与移动次数（验证 Result 不漏构造/析构、不双析构）
struct MoveTracker {
    static int live;
    int id;
    int moves;

    explicit MoveTracker(int i) : id(i), moves(0) { ++live; }
    MoveTracker(const MoveTracker& o) : id(o.id), moves(o.moves) { ++live; }
    MoveTracker(MoveTracker&& o) noexcept : id(o.id), moves(o.moves + 1)
    {
        ++live;
        o.id = -1; // 标记已移动
    }
    MoveTracker& operator=(const MoveTracker& o)
    {
        id = o.id;
        moves = o.moves;
        return *this;
    }
    MoveTracker& operator=(MoveTracker&& o) noexcept
    {
        id = o.id;
        moves = o.moves + 1;
        o.id = -1;
        return *this;
    }
    ~MoveTracker() { --live; }
};

int MoveTracker::live = 0;

} // namespace

TEST(result_success_holds_value)
{
    Result<int> r = Result<int>::success(42);
    CHECK(r.ok());
    CHECK(static_cast<bool>(r));
    CHECK_EQ(r.value(), 42);
    CHECK(r.error().ok());
}

TEST(result_failure_holds_error)
{
    Result<int> r = Result<int>::failure(ErrorCode::NotFound, "没了");
    CHECK(!r.ok());
    CHECK(!static_cast<bool>(r));
    CHECK(r.error().code() == ErrorCode::NotFound);
    CHECK_EQ(r.error().message(), std::string("没了"));
}

TEST(result_failure_takes_error_object)
{
    Error e(ErrorCode::IoError, "读失败");
    Result<std::string> r = Result<std::string>::failure(e);
    CHECK(!r.ok());
    CHECK(r.error().code() == ErrorCode::IoError);
}

TEST(result_success_moves_value_in)
{
    Result<MoveTracker> r = Result<MoveTracker>::success(MoveTracker(7));
    CHECK(r.ok());
    CHECK(r.value().id == 7);
    CHECK(MoveTracker::live >= 1);
}

TEST(result_move_constructor)
{
    MoveTracker::live = 0;
    {
        Result<MoveTracker> a = Result<MoveTracker>::success(MoveTracker(11));
        Result<MoveTracker> b(std::move(a));
        // 移动后源对象仍 ok()（值为「已移动」状态，内容未指定）
        CHECK(a.ok());
        CHECK(b.ok());
        CHECK(b.value().id == 11);
        CHECK(MoveTracker::live == 2); // 两个 Result 各持一个（源为已移动对象）
    }
    CHECK_EQ(MoveTracker::live, 0); // 全部析构，无泄漏无双析构
}

TEST(result_move_assignment)
{
    MoveTracker::live = 0;
    {
        Result<MoveTracker> a = Result<MoveTracker>::success(MoveTracker(1));
        Result<MoveTracker> b = Result<MoveTracker>::failure(ErrorCode::Internal, "x");
        b = std::move(a);
        CHECK(b.ok());
        CHECK(b.value().id == 1);
        CHECK(b.value().moves >= 1); // 经历过移动
    }
    CHECK_EQ(MoveTracker::live, 0);
}

TEST(result_failure_does_not_construct_value)
{
    MoveTracker::live = 0;
    {
        Result<MoveTracker> r =
            Result<MoveTracker>::failure(ErrorCode::Unsupported, "不支持");
        CHECK(!r.ok());
        CHECK_EQ(MoveTracker::live, 0); // 失败分支不应构造 T
    }
    CHECK_EQ(MoveTracker::live, 0);
}

TEST(result_copy_semantics)
{
    Result<std::string> a = Result<std::string>::success("hello");
    Result<std::string> b = a;
    CHECK(b.ok());
    CHECK_EQ(b.value(), std::string("hello"));
    CHECK_EQ(a.value(), std::string("hello"));

    Result<std::string> c = Result<std::string>::failure(ErrorCode::ParseError, "p");
    c = a;
    CHECK(c.ok());
    CHECK_EQ(c.value(), std::string("hello"));
}

TEST(result_void_success_and_failure)
{
    Result<void> ok = Result<void>::success();
    CHECK(ok.ok());

    Result<void> bad = Result<void>::failure(ErrorCode::PermissionDenied, "无权限");
    CHECK(!bad.ok());
    CHECK(bad.error().code() == ErrorCode::PermissionDenied);

    Result<void> viaCode = Result<void>::failure(ErrorCode::IoError, "io");
    CHECK(!viaCode.ok());
    CHECK_EQ(viaCode.error().toString(), std::string("IoError: io"));
}
