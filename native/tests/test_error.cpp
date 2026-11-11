// test_error.cpp — Error / errorCodeName / toString
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"

#include "readex/error.hpp"

using readex::Error;
using readex::ErrorCode;

TEST(error_default_is_ok)
{
    Error e;
    CHECK(e.ok());
    CHECK(e.code() == ErrorCode::Ok);
    CHECK(e.message().empty());
    CHECK_EQ(e.toString(), std::string("Ok"));
}

TEST(error_carries_code_and_message)
{
    Error e(ErrorCode::NotFound, "章节 99 不存在");
    CHECK(!e.ok());
    CHECK(e.code() == ErrorCode::NotFound);
    CHECK_EQ(e.message(), std::string("章节 99 不存在"));
    CHECK_EQ(e.toString(), std::string("NotFound: 章节 99 不存在"));
}

TEST(error_ok_code_clears_message)
{
    Error e(ErrorCode::Ok, "不应保留");
    CHECK(e.ok());
    CHECK(e.message().empty());
}

TEST(error_code_name_is_stable)
{
    CHECK_EQ(std::string(readex::errorCodeName(ErrorCode::InvalidArgument)),
             std::string("InvalidArgument"));
    CHECK_EQ(std::string(readex::errorCodeName(ErrorCode::Internal)),
             std::string("Internal"));
    CHECK_EQ(std::string(readex::errorCodeName(ErrorCode::OutOfMemory)),
             std::string("OutOfMemory"));
}

TEST(error_to_string_without_message)
{
    Error e(ErrorCode::IoError, "");
    CHECK_EQ(e.toString(), std::string("IoError"));
}
