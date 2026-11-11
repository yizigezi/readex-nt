// core.cpp — 最小核心实现（PR-1 冒烟用）
//
// SPDX-License-Identifier: GPL-3.0

#include "readex/core.hpp"

#include <stdexcept>

namespace readex {

const char* version() noexcept
{
    // 与 package.json "version" 同步
    return "0.1.0";
}

const char* quickjsHeaderVersion() noexcept
{
    // vendored 的 native/quickjs/quickjs.h 为 QuickJS 2020-07-05 官方头文件
    // （该版本头文件里没有 JS_VERSION 宏，故在此硬编码，勿换头文件版本）
    return "2020-07-05";
}

const char* platform() noexcept
{
#if defined(__arm__) && defined(__SOFTFP__)
    return "linux-armv7-softfp";
#elif defined(__arm__)
    // EABI5 hard-float（armhf）：__SOFTFP__ 未定义
    return "linux-armv7hf";
#elif defined(__aarch64__)
    return "linux-aarch64";
#elif defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__i386__)
    return "linux-x86";
#else
    return "linux-unknown";
#endif
}

const char* buildMode() noexcept
{
#ifdef NDEBUG
    return "release";
#else
    return "debug";
#endif
}

double add(double a, double b) noexcept
{
    return a + b;
}

void fail()
{
    // 冒烟演示：故意抛出 C++ 异常，验证 binding 层的异常→JS Error 转换。
    // 约束：此异常绝不能穿过 QuickJS C ABI（bindings.cpp 统一拦截）。
    throw std::runtime_error("Readex.fail 演示错误（C++ 异常已按约定转换为 JS Error）");
}

} // namespace readex
