// microtest.hpp — 零依赖 assert 式小测试框架（host 单元测试用）
//
// 用法：
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//   main 由 tests/main.cpp 提供（跑全部注册用例，异常也算失败）。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_MICROTEST_HPP
#define READEX_MICROTEST_HPP

#include <cstdio>
#include <string>
#include <vector>

namespace microtest {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({ name, fn }); }
};

// 单个用例内的失败计数（runner 汇总）
inline int& failCount()
{
    static int n = 0;
    return n;
}

inline void reportFail(const char* file, int line, const std::string& what)
{
    std::printf("    FAIL %s:%d: %s\n", file, line, what.c_str());
    ++failCount();
}

} // namespace microtest

#define TEST(name)                                            \
    static void microtest_case_##name();                      \
    static ::microtest::Registrar microtest_reg_##name(       \
        #name, &microtest_case_##name);                       \
    static void microtest_case_##name()

#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond))                                            \
            ::microtest::reportFail(__FILE__, __LINE__, #cond); \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        if (!((a) == (b)))                                                 \
            ::microtest::reportFail(__FILE__, __LINE__,                    \
                                    std::string(#a " == " #b));            \
    } while (0)

// 期望表达式抛出指定异常类型
#define CHECK_THROWS(expr, ExceptionType)                                     \
    do {                                                                      \
        bool microtest_caught = false;                                        \
        try {                                                                 \
            (void)(expr);                                                     \
        } catch (const ExceptionType&) {                                      \
            microtest_caught = true;                                          \
        } catch (...) {                                                       \
        }                                                                     \
        if (!microtest_caught)                                                \
            ::microtest::reportFail(__FILE__, __LINE__,                       \
                                    #expr " 应抛出 " #ExceptionType);         \
    } while (0)

#endif // READEX_MICROTEST_HPP
