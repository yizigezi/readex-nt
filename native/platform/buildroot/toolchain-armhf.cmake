# toolchain-armhf.cmake — 交叉工具链：有道词典笔 X5
#
# 目标环境（已验证的背景事实）：
#   OS: Buildroot / kernel 4.19
#   CPU: ARMv7 EABI5，hard-float（armhf）
#   libc: glibc 2.23
#   动态链接器: /lib/ld-linux-armhf.so.3
#
# 用法：
#   cmake -S native -B build-arm \
#       -DCMAKE_TOOLCHAIN_FILE=native/platform/buildroot/toolchain-armhf.cmake
#
# —— glibc 2.23 符号版本上限的钉法 ——
# 发行版的 arm-linux-gnueabihf-gcc 链接的是宿主交叉 sysroot 的新 glibc，
# 链接期无法保证不引用 GLIBC_2.24+ 的新符号（如 getrandom、reallocarray），
# 老固件上会直接加载失败。可选做法：
#   a) 优先用 zig：zig c++ -target arm-linux-gnueabihf.2.23
#      （zig 把 glibc 符号版本钉在 2.23，tools/build-core.sh 默认走这条）
#   b) 用 glibc 2.23 的 sysroot + 本工具链文件（把 CMAKE_SYSROOT 指过去）
# 无论走哪条，产物必须过 tools/build-core.sh --check：
#   GLIBC 符号版本上限 <= 2.23、NEEDED 只有 libc.so.6、hard-float、
#   导出 custom_init_jsapis。自检不过的产物严禁装入 libs/。
#
# SPDX-License-Identifier: GPL-3.0

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 传统 GNU 交叉工具链（armhf = hard-float 调用约定，Tag_ABI_VFP_args: VFP registers）
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 目标平台基线：ARMv7 + VFPv3-D16（X5 常见配置，如真机 CPU 不同按需调整）
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard")

# 不用宿主 sysroot 时（如配合 glibc 2.23 sysroot），打开下面一行并指到 sysroot：
# set(CMAKE_SYSROOT /opt/gcc-arm-linux-gnueabihf/glibc-2.23-sysroot)

# 只在目标树里找库/头文件，程序（cmake 自身跑的）永远用宿主的
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
