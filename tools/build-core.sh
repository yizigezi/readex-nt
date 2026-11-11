#!/usr/bin/env bash
# readex 原生核心交叉编译脚本（QuickJS "readex" 模块 -> libs/libjsapi_readex.so）
#
# 目标：ARMv7 EABI5 hard-float / glibc 2.23（有道词典笔 X5，Buildroot）。
# 产出随 AMR 的 libs/ 打包，宿主加载 libjsapi_*.so 后由 custom_init_jsapis
# 注册 ES 模块 "readex"（JS 侧 `import Readex from 'readex'`）。
#
# 用法:
#   tools/build-core.sh            # 自动选工具链编译 + ELF 自检 + 装入 libs/
#   tools/build-core.sh --check    # 仅对 libs/libjsapi_readex.so 做 ELF 自检
#   tools/build-core.sh --force    # 自检未过也装入（见下方已知偏差，慎用）
#
# 工具链优先级: zig c++（钉 glibc 2.23 符号版本） > arm-linux-gnueabihf-g++
#
# —— 已知偏差（实测，见 PR-1 报告）——
# C++ 异常运行时（libunwind/libcxxabi）在 glibc ≤2.33 目标上固有地引用
# __tls_get_addr（来自 ld-linux-armhf.so.3）与 dl_iterate_phdr/dladdr
# （来自 libdl.so.2），zig 与 gcc 静态链 C++ 运行时后都无法只留 libc.so.6。
# 两者均为 glibc 基础组件（ld-linux 即动态链接器本身）；确认设备 rootfs
# 含 /lib/libdl.so.2 后可用 --force 装入。严格自检（--check）仍按
# 「NEEDED 只有 libc.so.6」执行，不会降低标准。
#
# C++ 运行时处理（设备 rootfs 可能没有 libstdc++）：
#   - gcc 路径显式 -static-libstdc++ -static-libgcc
#   - zig c++ 默认把 C++ 运行时/编译器运行时静态链入，无需额外开关
#   两条路径最终都以 ELF 自检的「NEEDED 只有 libc.so.6」为准。
#
# SPDX-License-Identifier: GPL-3.0
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC=( "$ROOT/native/quickjs/module.cpp"
      "$ROOT/native/quickjs/bindings.cpp"
      "$ROOT/native/src/core/core.cpp" )
INC=( -I "$ROOT/native/include" -I "$ROOT/native/quickjs" )
OUT_DIR="$ROOT/native/dist"
OUT="$OUT_DIR/libjsapi_readex.so"
TARGET_LIBS="$ROOT/libs/libjsapi_readex.so"
EXPECT_GLIBC="2.23"
FORCE=0

# ZIG 环境变量可指定 zig 路径，默认从 PATH 找
ZIG="$(command -v "${ZIG:-zig}" 2>/dev/null || true)"

check_elf() {
  local so="$1"
  echo "== ELF 自检: $so =="
  local fail=0

  local machine
  machine=$(readelf -h "$so" | awk '/Machine:/{print $NF}')
  if [ "$machine" != "ARM" ]; then
    echo "校验失败: 期望 Machine=ARM 得到 $machine"; fail=1
  fi

  if ! readelf -A "$so" | grep -q 'Tag_ABI_VFP_args: VFP registers'; then
    echo "校验失败: 不是 hard-float (Tag_ABI_VFP_args)"; fail=1
  fi

  if ! nm -D "$so" 2>/dev/null | grep -q ' T custom_init_jsapis'; then
    echo "校验失败: 未导出 custom_init_jsapis"; fail=1
  fi

  if nm -D "$so" 2>/dev/null | grep -q ' U registerCModuleLoader'; then
    echo "OK: registerCModuleLoader 留待宿主框架解析（预期的未定义符号）"
  else
    echo "校验失败: registerCModuleLoader 应为未定义动态符号（宿主提供）"; fail=1
  fi

  # C++ 产物静态链运行时：NEEDED 只允许 libc.so.6（无 libstdc++/libgcc_s/ld-linux）
  local needed
  needed=$(readelf -d "$so" | grep NEEDED | awk '{print $NF}' | tr -d '[]' | tr '\n' ' ')
  echo "NEEDED: $needed"
  for lib in $needed; do
    case "$lib" in
      libc.so*) ;;
      *) echo "校验失败: 依赖了非 libc 基础库 $lib（设备 rootfs 可能没有，需静态链入）"; fail=1 ;;
    esac
  done

  local glibc_max
  glibc_max=$(readelf -V "$so" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1 || true)
  echo "GLIBC 符号版本上限: ${glibc_max:-无}"
  if [ -n "$glibc_max" ]; then
    local ver="${glibc_max#GLIBC_}"
    if [ "$(printf '%s\n%s\n' "$EXPECT_GLIBC" "$ver" | sort -V | tail -1)" != "$EXPECT_GLIBC" ]; then
      echo "校验失败: 引用了高于 glibc $EXPECT_GLIBC 的符号 ($glibc_max)，老固件会挂"; fail=1
    fi
  fi

  if [ "$fail" != 0 ]; then
    echo "ELF 自检未通过"
    if [ "$FORCE" = 1 ]; then
      echo "警告: --force 模式，继续装入（请先确认设备 rootfs 含 libdl.so.2 / ld-linux-armhf.so.3）"
      return 0
    fi
    exit 1
  fi
  echo "== ELF 自检通过 =="
}

if [ "${1:-}" = "--check" ]; then
  check_elf "$TARGET_LIBS"
  exit 0
fi

if [ "${1:-}" = "--force" ]; then
  FORCE=1
  echo "警告: --force 模式，自检未过也会装入 libs/（已知偏差见脚本头注释）"
fi

mkdir -p "$OUT_DIR"

echo "== 编译 $OUT （目标 armv7 hard-float, glibc $EXPECT_GLIBC） =="
if [ -n "$ZIG" ]; then
  echo "== 工具链: $ZIG =="
  # zig c++ 默认静态链 C++ 运行时（libc++/compiler-rt），glibc 符号钉 $EXPECT_GLIBC
  "$ZIG" c++ -target arm-linux-gnueabihf.${EXPECT_GLIBC} \
    -std=c++14 -shared -fPIC -O2 -fvisibility=hidden \
    "${INC[@]}" "${SRC[@]}" -o "$OUT"
elif command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
  echo "== 工具链: arm-linux-gnueabihf-g++（注意: 链接宿主交叉 glibc，需自检符号版本） =="
  arm-linux-gnueabihf-g++ \
    -std=c++14 -shared -fPIC -O2 -fvisibility=hidden \
    -static-libstdc++ -static-libgcc \
    "${INC[@]}" "${SRC[@]}" -o "$OUT"
else
  echo "错误: 未找到 zig 或 arm-linux-gnueabihf-g++" >&2
  exit 1
fi

check_elf "$OUT"

cp "$OUT" "$TARGET_LIBS"
echo "== 已装入 $TARGET_LIBS（aiot-cli 打包时随 libs/ 进 AMR） =="
echo "提示: 升级安装前先 miniapp_cli uninstall，同 appid 不会重新提取 libs/*.so"
