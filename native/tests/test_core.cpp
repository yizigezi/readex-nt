// test_core.cpp — 最小核心冒烟（version/add/fail）+ 接口头文件可编译性
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"

// 接口头文件（PR-2+ 的空壳）随测试一起编译，保证声明长期可编译
#include "readex/book.hpp"
#include "readex/core.hpp"
#include "readex/decoder.hpp"
#include "readex/error.hpp"
#include "readex/parser.hpp"
#include "readex/pagination.hpp"
#include "readex/search.hpp"

#include <stdexcept>
#include <string>

TEST(core_version_strings)
{
    CHECK(std::string(readex::version()) == "0.1.0");
    CHECK(std::string(readex::quickjsHeaderVersion()) == "2020-07-05");
    CHECK(!std::string(readex::platform()).empty());
    std::string mode = readex::buildMode();
    CHECK(mode == "debug" || mode == "release");
}

TEST(core_add)
{
    CHECK_EQ(readex::add(1, 2), 3.0);
    CHECK_EQ(readex::add(-1.5, 0.5), -1.0);
}

TEST(core_fail_throws_cxx_exception)
{
    CHECK_THROWS(readex::fail(), std::runtime_error);
}

TEST(core_fail_message_is_stable)
{
    try {
        readex::fail();
    } catch (const std::exception& e) {
        CHECK(std::string(e.what()).find("Readex.fail") != std::string::npos);
        return;
    }
    CHECK(false); // 不应到达
}
