# readex 原生核心构建系统（PR-1）
#
# 两种模式：
#   1) host 单元测试（默认，非交叉）：
#        cmake -S native -B build-host
#        cmake --build build-host
#        ctest --test-dir build-host        # 或直接跑 build-host/readex_tests
#   2) 交叉编译设备 .so（armhf / glibc 2.23）：
#        cmake -S native -B build-arm \
#            -DCMAKE_TOOLCHAIN_FILE=native/platform/buildroot/toolchain-armhf.cmake
#        cmake --build build-arm            # 产出 libjsapi_readex.so
#        # 之后必须跑 tools/build-core.sh --check 做 ELF 自检并装入 libs/
#
# 也可不经 CMake 直接用 tools/build-core.sh（镜像 build-fs-plugin.sh 的流程）。
#
# SPDX-License-Identifier: GPL-3.0

cmake_minimum_required(VERSION 3.10)
project(readex-native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(READEX_BUILD_QUICKJS_MODULE "构建 QuickJS 原生模块 libjsapi_readex.so" ON)
if(CMAKE_CROSSCOMPILING)
    option(READEX_BUILD_TESTS "构建 host 单元测试" OFF)
else()
    option(READEX_BUILD_TESTS "构建 host 单元测试" ON)
endif()

# ---- 核心静态库（不依赖 QuickJS，可在普通 C++ 测试里独立运行）----
# reader/decoder 仅用 libc（decoder 的 GBK 走 glibc 内建 iconv，不新增依赖）
add_library(readex_core STATIC
    src/core/core.cpp
    src/reader/reader.cpp
    src/decoder/decoder.cpp
    src/parser/parser.cpp
    src/book/book.cpp
    src/book/book_registry.cpp
)
target_include_directories(readex_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# 核心会被链进共享库 jsapi_readex：必须 PIC（PR-2 实测 host x86_64 链接报
# R_X86_64_PC32 … recompile with -fPIC；zig c++ 路径默认 PIC 故未暴露）
set_target_properties(readex_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- QuickJS 原生模块（设备 .so / host 冒烟均可编译）----
if(READEX_BUILD_QUICKJS_MODULE)
    add_library(jsapi_readex SHARED
        quickjs/module.cpp
        quickjs/bindings.cpp
        quickjs/book_binding.cpp
    )
    target_include_directories(jsapi_readex PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/quickjs
    )
    target_link_libraries(jsapi_readex PRIVATE readex_core)

    # 只导出 custom_init_jsapis（其自身带 visibility("default") 属性）
    set_target_properties(jsapi_readex PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        OUTPUT_NAME "jsapi_readex"   # -> libjsapi_readex.so，命名符合宿主 libjsapi_*.so 约定
        PREFIX "lib"
    )

    # 设备 rootfs 可能没有 libstdc++/libgcc_s：C++ 运行时必须静态链入，
    # ELF 自检要求 NEEDED 只有 libc.so.6（tools/build-core.sh --check）。
    # 注意：zig c++ 本就静态链 C++ 运行时；这两个开关针对 gcc 系工具链。
    set_property(TARGET jsapi_readex APPEND_STRING PROPERTY
        LINK_FLAGS " -static-libstdc++ -static-libgcc")
endif()

# ---- host 单元测试（零第三方依赖，assert 式小框架）----
if(READEX_BUILD_TESTS)
    enable_testing()
    add_executable(readex_tests
        tests/main.cpp
        tests/test_error.cpp
        tests/test_result.cpp
        tests/test_exception.cpp
        tests/test_core.cpp
        tests/test_reader.cpp
        tests/test_decoder.cpp
        tests/test_parser.cpp
        tests/test_book.cpp
        tests/test_book_registry.cpp
    )
    target_include_directories(readex_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(readex_tests PRIVATE readex_core)
    add_test(NAME readex_tests COMMAND readex_tests)
endif()
