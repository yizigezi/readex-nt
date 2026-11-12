# readex-nt

为有道词典笔 OS 设计的第三方阅读器，全新架构。项目框架参照 readex（doge-reader fork）搭建。

## 与 readex / doge-reader 的关系

- 本项目为**全新实现**，仅参照 readex 的项目结构、构建链路与目标平台约定。
- ⚠️ 许可注意：上游 doge-reader / readex 以 **AGPL-3.0** 发布。本项目以 GPL-3.0 发布，因此**不得直接复制上游源码**（页面、组件、工具层等均须独立实现）；若确需引用上游代码，被引用部分及其衍生须继续按 AGPL-3.0 发布（GPL-3.0 §13）。
- `native/quickjs/quickjs.h` 为 QuickJS（MIT）官方头文件，与上游无关。

## 目标设备

| 项 | 值 |
|---|---|
| 设备 | 有道词典笔 X5（PenOS） |
| 架构 | ARMv7 EABI5，hard-float（armhf） |
| libc | glibc 2.2x |
| QuickJS | 2020-07-05（宿主自带，`package.json` 中 `quickjs.version` 须与此一致） |
| 应用框架 | Falcon（`$falcon`），构建工具 `aiot-cli` |
| 安装方式 | `.amr` 包经 adb 推送，`miniapp_cli install/start` |

## 开发

```bash
npm install
npm run build:dev     # 开发构建
npm run build:prod    # 生产构建（.amr）
./install.sh          # 构建并安装到已连接的词典笔
```

## 目录结构

```
readex-nt/
├── app_icon.png          # 应用图标（当前为占位）
├── install.sh            # 构建 + adb 安装
├── native/
│   ├── CMakeLists.txt    # 构建系统（host 单元测试 + 交叉编译设备 .so 两种模式）
│   ├── fs_plugin.c       # fs 原生模块（QuickJS C 模块，导出 custom_init_jsapis）
│   ├── include/readex/   # 统一错误类型 + 核心接口头（book/parser/pagination/search 为后续 PR 空壳，reader/decoder 已落地）
│   ├── src/core/         # 最小核心（版本/平台信息、冒烟演示，不依赖 QuickJS）
│   ├── quickjs/          # QuickJS 2020-07-05 头文件 + readex 模块绑定骨架
│   ├── platform/buildroot/ # armhf 交叉工具链文件
│   └── tests/            # C++ 单元测试（零依赖 assert 式小框架）
├── libs/                 # 原生 .so 插件（随 AMR 打包）
│   └── libjsapi_fs.so    # fs 插件产物（armhf / hard-float / glibc 2.23）
├── tools/
│   ├── build-fs-plugin.sh  # fs 插件编译 + ELF 自检（--check 仅自检）
│   └── build-core.sh       # readex 核心模块编译 + ELF 自检（--check 仅自检）
└── src/
    ├── app.js            # 应用入口（$falcon.App）
    ├── app.json          # 页面注册
    ├── base-page.js      # 页面基类（资源清理）
    ├── assets/           # 图片资源
    ├── pages/            # 页面
    ├── styles/           # 样式与色板
    └── utils/
        └── FileSystem/   # fs 兼容适配层（动态 import('fs')，execShell 兜底）
```

## 原生 fs 插件

部分固件不带 `fs` jsapi，`native/fs_plugin.c` 注册同名 C 模块兜底（Promise 风格 readdir/stat/exists/mkdir/rm/unlink/readFile/writeFile）。JS 侧统一走 `src/utils/FileSystem/FileSystem.js` 动态适配，原生缺失时自动退回 execShell。重新编译：

```bash
tools/build-fs-plugin.sh          # 编译 + ELF 自检并装入 libs/
tools/build-fs-plugin.sh --check  # 仅对 libs/libjsapi_fs.so 自检
```

## Native Core（原生核心，重构中）

按《Readex Native Core 原生化重构计划》（docs/）分 PR 推进。当前进度：**PR-2（文本读取与编码模块）已完成**。

### PR-1 冒烟 API

JS 侧 `import Readex from 'readex'`：

| API | 返回 | 说明 |
|---|---|---|
| `Readex.version()` | string | 原生核心版本 |
| `Readex.add(a, b)` | number | 缺参/非数字抛 JS TypeError，message 写明期望与实际类型 |
| `Readex.info()` | object | `{ version, quickjs, platform, buildMode }` |
| `Readex.fail()` | 恒抛 JS Error | 演示 C++ 异常 → JS Error 的转换（异常绝不穿过 QuickJS C ABI） |

> **模块名偏差说明**：计划书 §四草案写 `import Readex from '@readex/core'`，
> 但模块名以宿主 `registerCModuleLoader` 的注册名为准（与 fs 插件同一套 jsapi ABI），
> 实际注册名为 **`readex`**。

### 构建

**host 单元测试**（g++ 直接编译，或经 CMake）：

```bash
g++ -std=c++14 -I native/include -I native/tests \
    native/tests/*.cpp native/src/core/*.cpp native/src/reader/*.cpp native/src/decoder/*.cpp \
    -o native/tests/readex_tests
./native/tests/readex_tests

# 或 CMake：
cmake -S native -B build-host && cmake --build build-host
cmake --build build-host --target test   # 或直接跑 build-host/readex_tests
```

**交叉编译设备 .so**（产出 `libs/libjsapi_readex.so`）：

```bash
tools/build-core.sh          # 编译 + ELF 自检并装入 libs/（工具链优先 zig c++）
tools/build-core.sh --check  # 仅自检
cmake -S native -B build-arm -DCMAKE_TOOLCHAIN_FILE=native/platform/buildroot/toolchain-armhf.cmake
```

ELF 自检项：Machine=ARM、hard-float（Tag_ABI_VFP_args）、导出 `custom_init_jsapis`、
NEEDED 只在 glibc 基础库白名单（libc / libdl / libm / ld-linux，C++ 运行时静态链入）、
GLIBC 符号版本 ≤ 2.23。

> **已知偏差（实测）**：C++ 异常运行时（libunwind/libcxxabi）在 glibc ≤2.33 目标上
> 固有地引用 `__tls_get_addr`（`ld-linux-armhf.so.3`）与 `dl_iterate_phdr`/`dladdr`
> （`libdl.so.2`），zig 与 gcc 静态链后都无法只留 libc.so.6。两者均为 glibc 基础组件。
> `tools/build-core.sh --check` 仍按「只有 libc.so.6」严格拦截；确认设备 rootfs
> 含 `/lib/libdl.so.2` 后用 `--force` 装入。另：Ubuntu 新版交叉 gcc 的静态 libstdc++
> 会引用 GLIBC_2.3x 新符号，真机必挂，务必用 zig（或 glibc 2.23 sysroot）并过自检。

### host 冒烟宿主（QuickJS 2020-07-05）

`native/tests/host-smoke/` 提供模拟设备 jsapi 加载 ABI 的宿主，可在 PC 上真实跑通
冒烟 API（dlopen .so → custom_init_jsapis → registerCModuleLoader → `import Readex from 'readex'`）：

```bash
# 获取与 vendored 头文件同版本的 QuickJS 源码（quickjs.c 不入库）
curl -LO https://bellard.org/quickjs/quickjs-2020-07-05.tar.xz && tar xf quickjs-2020-07-05.tar.xz

gcc -O2 -rdynamic -DCONFIG_VERSION=\"2020-07-05\" -I quickjs-2020-07-05 \
    quickjs-2020-07-05/{quickjs,libregexp,libunicode,cutils}.c \
    native/tests/host-smoke/harness.c -o smoke_host -ldl -lm

# 先编 host 版 .so（把 JS_* 符号留给宿主）
g++ -std=c++14 -shared -fPIC -fvisibility=hidden -O2 \
    -I native/quickjs -I native/include \
    native/quickjs/*.cpp native/src/core/core.cpp -o libjsapi_readex_host.so

./smoke_host ./libjsapi_readex_host.so native/tests/host-smoke/smoke.mjs
```

### PR-2 文本读取与编码（FileReader + TextDecoder）

两个纯 C++ 模块（不依赖 QuickJS，host 单元测试独立可跑）：

**FileReader**（`native/include/readex/reader.hpp` + `native/src/reader/`）——流式文件读取：

- `open` / `read` / `seek` 返回 `Result<T>`（复用 PR-1 统一错误体系），`tell` / `size` 直接返回值；RAII，析构自动 close，重复 close 安全，不泄漏 fd。
- 64 位偏移（LFS，`lseek64`），支持大文件分块处理；不提供整读接口，禁止把整本书读进内存。
- 错误映射：文件不存在→`NotFound`、无权限→`PermissionDenied`、路径是目录/读写失败→`IoError`、未打开句柄上操作→`InvalidArgument`。

**TextDecoder**（`native/include/readex/decoder.hpp` + `native/src/decoder/`）——编码识别与转换：

- 支持 UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE / GBK（含 GB2312）。
- **有状态解码器**：`create` / `feed(chunk)` / `finish()`，多字节字符跨块不乱；计划书的一次性
  `decode(data, len, encoding)` 签名保留为便捷包装（偏差理由：分块需求要求有状态，见 `decoder.hpp` 头注释）。
- `detect(FileReader&)` 采样文件头尾各 ≤64KiB：BOM → UTF-16 零字节奇偶模式 → UTF-8 合法性 → 兜底 GBK，
  不只看文件头。已知局限：无 BOM 的纯中文 UTF-16（无零字节线索）无法与 GBK 可靠区分，会兜底 GBK，
  请优先用带 BOM 的 UTF-16；UTF-32 BOM 明确返回 `Unsupported` 错误。

**非法字节序列策略**（有测试覆盖）：一律替换为 U+FFFD；UTF-8/GBK 每处非法序列前进 1 字节重同步
（非法序列后的合法字节不被吞掉），UTF-16 孤立代理项同样替换；块末尾未完成序列保留到下一块、
`finish()` 时补 U+FFFD；中文标点（，。！？「」——…）在任何切分位置不截断。绝不崩溃、不死循环。

**GBK 选型**：GBK→UTF-8 走 glibc 内建 `iconv()`（`libc.so.6` 自带，ELF NEEDED 不变、
`iconv*@GLIBC_2.4`，天然 ARMv7 / glibc 2.23 兼容，已用 zig 交叉链接验证）。
已知风险：极简 rootfs 若缺 gconv 模块（`/usr/lib/gconv`），`iconv_open` 会失败——此时
`create()` 返回 `DecodeError`、一次性 `decode()` 返回空串；真机验证时需确认该目录存在，
否则回退为内嵌 GBK 码表。UTF-8 / UTF-16 为手写增量转换（零外部依赖）。

**性能探针**（host x86_64，非设备指标）：1.7MiB 混合文本 detect ≈ 2ms；64KiB 块流式读 + GBK 解码 ≈ 55–100MiB/s。

### 接口头（后续 PR 空壳）

`native/include/readex/` 下 `book.hpp` / `parser.hpp` / `pagination.hpp` / `search.hpp`
只有接口声明与说明（`decoder.hpp` 已于 PR-2 落地），实现按 PR-3…PR-5 逐步落地，不以空实现冒充完成。

### 内存与资源管理规范（PR-1 起生效）

- 可恢复错误一律走 `readex::Result<T>` / `readex::Error`（`native/include/readex/error.hpp`），不把异常当控制流。
- C++ 异常绝不穿过 QuickJS C ABI：binding 层每个 C 入口统一 `try/catch(...)`，经 `readex::currentExceptionToError()` 转成 JS Error。
- 文件句柄/原生资源一律 RAII 管理；不把内部指针/对象地址直接暴露给 JS，JS 侧只拿受管理句柄（PR-3 起）。
- 不一次性把整本书读进内存；大文本用流式/分块接口（PR-2 起）。

## 许可

[GPL-3.0](LICENSE)
