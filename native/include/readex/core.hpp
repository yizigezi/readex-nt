// core.hpp — 最小核心：版本/平台/构建信息 + 冒烟演示入口（PR-1）
//
// 冒烟 API（Readex.version/add/info/fail）所需的纯 C++ 逻辑都放在这里，
// 不依赖 QuickJS，可在 host 单元测试中直接调用。
//
// SPDX-License-Identifier: GPL-3.0

#ifndef READEX_CORE_HPP
#define READEX_CORE_HPP

namespace readex {

// readex 原生核心版本号（与 package.json version 保持同步）
const char* version() noexcept;

// 本 .so 编译时的 QuickJS 头文件版本（宿主 QuickJS 2020-07-05）
const char* quickjsHeaderVersion() noexcept;

// 目标平台标识，如 "linux-armv7hf" / "linux-x86_64"（按编译宏探测）
const char* platform() noexcept;

// 构建模式："debug" / "release"（按 NDEBUG 探测）
const char* buildMode() noexcept;

// 冒烟演示：加法。参数合法性（类型/缺参）由 binding 层负责校验。
double add(double a, double b) noexcept;

// 冒烟演示：恒抛出 C++ 异常（std::runtime_error）。
// 用于验证 binding 层把 C++ 异常转换成 JS Error 的通路；
// 后续 PR 的真实功能不会以异常作为控制流。
[[noreturn]] void fail();

} // namespace readex

#endif // READEX_CORE_HPP
