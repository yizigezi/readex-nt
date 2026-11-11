// main.cpp — 测试入口：跑全部注册用例
//
// SPDX-License-Identifier: GPL-3.0

#include "microtest.hpp"

#include <cstdio>

int main()
{
    int failedCases = 0;
    int total = static_cast<int>(microtest::registry().size());

    for (const microtest::Case& c : microtest::registry()) {
        int before = microtest::failCount();
        std::printf("[ RUN ] %s\n", c.name);
        try {
            c.fn();
        } catch (...) {
            microtest::reportFail(__FILE__, __LINE__,
                                  std::string("用例抛出未捕获异常: ") + c.name);
        }
        if (microtest::failCount() > before) {
            std::printf("[ FAIL] %s\n", c.name);
            ++failedCases;
        } else {
            std::printf("[ OK  ] %s\n", c.name);
        }
    }

    std::printf("======== %d 个用例, %d 个失败 ========\n", total, failedCases);
    return failedCases == 0 ? 0 : 1;
}
