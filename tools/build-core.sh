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
# —— 依赖策略（实测确定）——
# C++ 异常运行时（libunwind/libcxxabi）在 glibc ≤2.33 目标上固有地引用
# __tls_get_addr（来自 ld-linux-armhf.so.3）与 dl_iterate_phdr/dladdr
# （来自 libdl.so.2），zig 与 gcc 静态链 C++ 运行时后都无法只留 libc.so.6。
# 因此 ELF 自检的 NEEDED 白名单为 glibc 基础设施：libc / libdl / libm /
# ld-linux-*（动态链接器本身，必有）；C++ 运行时（libstdc++ / libgcc_s /
# libc++）仍为禁止项——设备 rootfs 可能没有它们。
#
# C++ 运行时处理：
#   - gcc 路径显式 -static-libstdc++ -static-libgcc
#   - zig c++ 默认把 C++ 运行时/编译器运行时静态链入，无需额外开关
#
# SPDX-License-Identifier: GPL-3.0
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC=( "$ROOT/native/quickjs/module.cpp"
      "$ROOT/native/quickjs/bindings.cpp"
      "$ROOT/native/quickjs/book_binding.cpp"
      "$ROOT/native/quickjs/pagination_binding.cpp"
      "$ROOT/native/quickjs/search_binding.cpp"
      "$ROOT/native/quickjs/state_binding.cpp"
      "$ROOT/native/src/core/core.cpp"
      "$ROOT/native/src/reader/reader.cpp"
      "$ROOT/native/src/decoder/decoder.cpp"
      "$ROOT/native/src/parser/parser.cpp"
      "$ROOT/native/src/pagination/pagination.cpp"
      "$ROOT/native/src/search/search.cpp"
      "$ROOT/native/src/state/reading_state.cpp"
      "$ROOT/native/src/book/book.cpp"
      "$ROOT/native/src/book/book_registry.cpp" )
INC=( -I "$ROOT/native/include" -I "$ROOT/native/quickjs" )
OUT_DIR="$ROOT/native/dist"
OUT="$OUT_DIR/libjsapi_readex.so"
TARGET_LIBS="$ROOT/libs/libjsapi_readex.so"
EXPECT_GLIBC="2.23"
FORCE=0

# 工具链解析：ZIG 环境变量 > PATH 里的 zig > pip 包 ziglang（python3 -m ziglang）
if [ -n "${ZIG:-}" ] && command -v "$ZIG" >/dev/null 2>&1; then
  ZIG_CMD=("$ZIG")
elif command -v zig >/dev/null 2>&1; then
  ZIG_CMD=(zig)
elif python3 -c "import ziglang" >/dev/null 2>&1; then
  ZIG_CMD=(python3 -m ziglang)
else
  ZIG_CMD=()
fi

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

  # NEEDED 白名单：仅 glibc 基础设施（libc/libdl/libm/ld-linux）；
  # C++ 运行时（libstdc++/libgcc_s/libc++）必须静态链入，禁止出现
  local needed
  needed=$(readelf -d "$so" | grep NEEDED | awk '{print $NF}' | tr -d '[]' | tr '\n' ' ')
  echo "NEEDED: $needed"
  for lib in $needed; do
    case "$lib" in
      libc.so*|libdl.so*|libm.so*|ld-linux-*) ;;
      libstdc++*|libgcc_s*|libc++*)
        echo "校验失败: 依赖了 C++ 运行时 $lib（设备 rootfs 可能没有，须静态链入）"; fail=1 ;;
      *) echo "校验失败: 依赖了非白名单库 $lib"; fail=1 ;;
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
      echo "警告: --force 模式，继续装入（自检项仍未过，请人工确认）"
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
if [ "${#ZIG_CMD[@]}" -gt 0 ]; then
  echo "== 工具链: ${ZIG_CMD[*]} =="
  # zig c++ 默认静态链 C++ 运行时（libc++/compiler-rt），glibc 符号钉 $EXPECT_GLIBC
  "${ZIG_CMD[@]}" c++ -target arm-linux-gnueabihf.${EXPECT_GLIBC} \
    -std=c++14 -shared -fPIC -O2 -g0 -fvisibility=hidden -ffile-prefix-map="$ROOT"=. -Wl,--strip-all \
    "${INC[@]}" "${SRC[@]}" -o "$OUT"
elif command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
  echo "== 工具链: arm-linux-gnueabihf-g++（注意: 链接宿主交叉 glibc，需自检符号版本） =="
  arm-linux-gnueabihf-g++ \
    -std=c++14 -shared -fPIC -O2 -g0 -fvisibility=hidden -ffile-prefix-map="$ROOT"=. -Wl,--strip-all \
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
