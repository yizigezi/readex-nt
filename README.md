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
│   ├── include/readex/   # 统一错误类型 + 核心接口头（reader/decoder/book/parser/pagination/search/reading_state 全部落地）
│   ├── src/core/         # 最小核心（版本/平台信息、冒烟演示，不依赖 QuickJS）
│   ├── src/book/         # Book Manager + 受管理句柄表（PR-3，Book 持有 ReadingStateStore）
│   ├── src/parser/       # 章节解析：规则表 + 标题匹配 + 字节偏移扫描（PR-3）
│   ├── src/pagination/   # 分页引擎 + 估算测量模型（PR-4）
│   ├── src/search/       # 全文搜索（PR-5，朴素字符串搜索）
│   ├── src/state/        # 阅读状态：进度/书签/JSON 序列化（PR-5）
│   ├── quickjs/          # QuickJS 2020-07-05 头文件 + readex 模块绑定（含 Book 对象绑定）
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

按《Readex Native Core 原生化重构计划》（docs/）分 PR 推进。当前进度：**PR-5（搜索 / 阅读状态 / 书签）已完成**（PR-6 Vue 层清理未动）。

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
    native/tests/*.cpp native/src/{core,reader,decoder,parser,book}/*.cpp \
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
> （`libdl.so.2`），zig 与 gcc 静态链后都无法只留 libc.so.6。两者均为 glibc 基础组件，
> 自检白名单已据此放行（libc / libdl / libm / ld-linux），C++ 运行时仍为禁止项。
> 另：Ubuntu 新版交叉 gcc 的静态 libstdc++
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
    native/quickjs/*.cpp native/src/{core,reader,decoder,parser,book,pagination,search,state}/*.cpp \
    -o libjsapi_readex_host.so

./smoke_host ./libjsapi_readex_host.so native/tests/host-smoke/smoke.mjs
./smoke_host ./libjsapi_readex_host.so native/tests/host-smoke/smoke_pr3.mjs <测试书txt>
./smoke_host ./libjsapi_readex_host.so native/tests/host-smoke/smoke_pr4.mjs <测试书txt>
./smoke_host ./libjsapi_readex_host.so native/tests/host-smoke/smoke_pr5.mjs <测试书txt>
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

### PR-3 书籍与章节管理（Book Manager + 章节解析 + QuickJS Book 绑定）

纯 C++ 部分（`book.hpp` + `src/book/`、`parser.hpp` + `src/parser/`）不依赖 QuickJS，
host 单元测试独立可跑；绑定层（`quickjs/book_binding.cpp`）只做参数校验与结果包装。

**JS API 形状**（`import Readex from 'readex'`；宿主 QuickJS 头 2020-07-05 提供
`JS_NewClass`/`JS_SetOpaque` 等生命周期 API，实测可编译可用，故 Book 采用
**JS 对象持有句柄**方案（类名 `Book`），未退化为裸 handle id + 顶层方法）：

| API | 返回 | 说明 |
|---|---|---|
| `Readex.openBook(path)` | Book 对象 | 缺参/非字符串→TypeError；文件不存在→Error `code=NotFound` |
| `book.getInfo()` | object | `{ title, path, sizeBytes, encoding, chapterCount }`；`title` = 文件名去扩展名，`encoding` 为 `utf-8`/`utf-8-bom`/`utf-16le`/`utf-16be`/`gbk` |
| `book.getChapters()` | array | `[{ title, index, startOffset, endOffset }]`，字节偏移（number） |
| `book.getChapter(i)` | object | **形状固定为 `{ title, index, text }`**，`text` 为解码后的 UTF-8 字符串（含标题行，保留区间原文） |
| `book.close()` | undefined | 幂等；close 后业务方法报 Error `code=InvalidHandle` |
| `Readex.closeAllBooks()` | number | 关闭并释放全部 Book，返回数量（测试便利） |

**生命周期**：JS 对象 opaque 只持句柄 id，Book 实例统一由受管理句柄表
（`book_registry.hpp`，不暴露 C++ 指针）持有；显式 `close()` 或对象被 GC
（class finalizer）都会释放条目（host 冒烟已用强制 GC 验证）。

**章节识别（TXT）**：规则与解析逻辑分离，规则表集中在
`parser.cpp defaultChapterRules()` 一处，可整体注入自定义规则（`scanChapters` 重载）：

- Numbered：`第X章/节/回/卷/篇/部/集/幕`、`Chapter X`（大小写不敏感）；
  数字支持阿拉伯/全角/中文（一二三十百千万零两…）；
- Keyword：`序章/序言/序/前言/引子/楔子/番外/番外篇/尾声/后记/附录/终章/终卷/大结局`；
- 标题行判定在**逐行解码后的 UTF-8 行文本**上做，字节偏移由**字节层编码感知行扫描**
  保留（UTF-8/GBK 按 `0x0A`、UTF-16 按 `0x000A` 码元切行，多字节字符不会跨行界）；
- 防正文误判：句末标点（。！？…）结尾的行视为句子不入目录；标题余部须以
  分隔符/中文数字开头，无分隔符直连仅对短行（≤12 码点）生效；超长行（>512B）必为正文。
  已知局限：极短正文行（如「第一章鱼」）可能误判，与主流阅读器行为一致。

**偏移与区间语义**（PR-4 分页将依赖）：章节区间 `[startOffset, endOffset)` 首尾
相接、无缝无重覆盖全书；标题章的 `startOffset` 指向标题行首字节；标题前的非空白
内容成为第 0 章（title = 书名）；纯空白前缀并入第一章节（该章 startOffset = 0）；
无章节文本 → 整本单章（title = 书名）；空文件 → 单个空章节。

**性能/内存**：openBook 只做顺序扫描保留章节元数据（不驻留正文）；getChapter 经
`FileReader.seek` 回读 + 64KiB 分块解码。host 探针（x86_64，非设备指标）：
16MiB 书 openBook 的 RSS 增量 < 4MiB（测试断言，ASan 下跳过）。

**host 冒烟**（QuickJS 2020-07-05 真实运行，非真机验证）：`smoke_pr3.mjs` 覆盖
Book 全生命周期、章节内容/偏移、错误码（NotFound/InvalidHandle/TypeError/RangeError）、
多书并行、closeAllBooks 与 GC finalizer 清理；另跑 `smoke.mjs` 确认 PR-1 冒烟 API 无回归。
设备真机验证待 PR-4 一并进行。

### PR-4 分页引擎（Pagination Engine + 测量解耦 + QuickJS 绑定 + Vue 毛坯接线）

纯 C++ 部分（`pagination.hpp` + `src/pagination/`）不依赖 QuickJS，host 单测独立可跑；
绑定层（`quickjs/pagination_binding.cpp`）只做参数校验与结果包装。

**偏移语义（PR-5 依赖的关键约定）**：`PageInfo.startOffset / endOffset` 是「传入
content 字符串内的**解码文本偏移**」——即 UTF-8 字节偏移，指向 `paginate()` 收到的
那串已解码章文本，**不是**原始文件字节偏移（文件偏移是 ChapterInfo 的语义）。
恒满足：半开区间 `[start, end)`、恒在码点边界、各页区间首尾相接无缝无重（逐页
substr 拼接 == 原文，不丢字、不重复）、`pageIndex` 从 0 递增；空文本返回单个
空页 `[0, 0)`（UI 总有页可显示）。PR-5 的 ReadingState 将存 `chapterIndex +
textOffset`，其中 `textOffset` 沿用本约定；全文搜索返回的匹配偏移同样复用。
JS 侧注意：JS 字符串下标 ≠ UTF-8 字节偏移（中文 1 字 = 3 字节），按偏移切片
需自行换算（`src/pages/reader/index.vue` 有换算示例）。

**JS API 形状**（`import Readex from 'readex'`；主形状是顶层 `Readex.paginate`）：

| API | 返回 | 说明 |
|---|---|---|
| `Readex.paginate(content, config)` | array | **主形状**：`[{ startOffset, endOffset, pageIndex }]`；content 为一章的已解码文本（UTF-8 字符串） |
| `book.paginateChapter(i, config)` | array | 便捷方法（内部 getChapter + paginate），结果与顶层逐字节一致；close 后调用报 `InvalidHandle` |

**config 字段与校验策略**（host 冒烟已测到）：`width`/`height` 必填（**可用阅读区域
由 UI 显式传入**，引擎不假设屏幕比例/工具栏宽度——这是修「内容挤在屏幕左侧」的
关键）；其余字段缺省用默认值，未知字段忽略；字段存在但不是数字 → `TypeError`；
数值非法（≤0、NaN、inf、边距吃光可用区等）→ `Error code=InvalidArgument`
（核心 `validatePaginationConfig` 统一判）；缺必填字段 → `Error code=InvalidArgument`。

| 字段 | 默认 | 说明 |
|---|---|---|
| `width` / `height` | （必填） | 可用阅读区域像素宽高 |
| `fontSize` | 20 | 字号（px） |
| `lineHeight` | 1.5 | 行高倍数（行高 = fontSize × lineHeight） |
| `letterSpacing` | 0 | 每字后附加 px（与 CSS letter-spacing 一致，行末也计） |
| `paragraphSpacing` | 0 | 段落（硬换行）间附加 px；不跨页（新页页顶不补） |
| `paddingTop/Bottom/Left/Right` | 0 | 四边边距 px |
| `asciiWidthEm` | 0.5 | ASCII/半角 em 系数 |
| `cjkWidthEm` | 1.0 | CJK 全角 em 系数 |
| `punctuationWidthEm` | 1.0 | 中文标点（1.0=全宽，0.5=半宽，可配） |

**字体测量解耦**：分页算法只依赖 `TextMeasurer` 抽象（码点 → 字形宽度），第一阶段
用 `EstimatingMeasurer`（可配置估算模型：CJK 全宽 / ASCII 半宽 / 标点全宽或半宽可配，
参数如上表后三行）。**明确局限**：估算宽度与真机字体渲染存在误差（0.5em/1em 是经验
系数，真机字体 advance 可能偏离）；不建模 kerning/连字/基线。真机若能提供字体测量
接口，实现 `TextMeasurer` 后走 `paginate(content, config, measurer)` 重载注入，分页
算法无需改动（C++ 层 API；JS 侧暂不暴露自定义测量器）。

**换行规则（基础版）**：只在合法断点软换行——句号逗号等收尾标点（，。！？；：、）」』…——）
不落行首、引号括号（「『（【《）不落行尾（靠超宽时向后回溯到最后合法断点实现）、
英文词内不断、成对连字（—— ……）中间不断、空白之后才断。**明确局限**（不崩、稳定、
但不完美）：行内无合法断点时强制断行（超长英文单词被机械切开）或把行首禁则字悬挂
在行尾（最多 1 字）；连续禁则字符（如「…………」）在极窄行宽下仍可能落行首；不做
悬挂标点/断词/两端对齐；`\t` 按 4 个半角空格宽估；非法 UTF-8 字节按 1 字节 1 字符
处理。其余边界：单字超宽独占一行（允许溢出）、每页至少 1 行（保证推进）、纯空白/
CRLF/超长行/极大文本不崩。

**Vue 毛坯接线**（`src/pages/reader/index.vue`）：阅读区宽高由 Vue 层显式传入——
falcon 暂无布局测量 API，毛坯先用「屏幕常量 × 布局占比」换算 px 并在注释写明
**「真机尺寸数据到位后校准」**；`Readex.paginate` 走动态 `import('readex')`（PC 构建
降级不白屏）。**不声称真机效果**：内容「不再挤在左侧」的真机验收由主人在 X5 上做。

**测试证据**（host x86_64）：`test_pagination.cpp` 21 个用例（全仓 108 个用例全绿，
含 PR-1…PR-3 回归）：黄金切分（句号不落行首/引号不落行尾/词内不断/字距/段距/边距）、
稳定性（同输入两次逐字段一致）、无缝拼接性质（伪随机中英混排 × 多配置，逐页 substr
拼接 == 原文 + 偏移恒在码点边界）、空/纯空白/CRLF/超长行/单字超宽/病态标点行、
配置边界（宽高 0/负/NaN/inf、字号 0、字距负、边距吃光可用区等均报 `InvalidArgument`）、
重新分页、估算参数（标点全宽 vs 半宽改变切分）、自定义测量器注入、分类/换行性质表；
ASan+UBSan 全绿；绑定层 host 冒烟（`smoke_pr4.mjs`，QuickJS 2020-07-05 真实运行）覆盖
两形状一致性、拼接性质（含 UTF-8 字节偏移 ↔ JS 下标换算）、避头尾黄金、配置校验策略、
NotFound/InvalidHandle/TypeError/RangeError 路径。大文本探针（记录用，非指标）：约
78 万字节中英混排（30 万字量级）单次分页 ≈ 26 ms（-O2）/ 70 ms（ASan）。设备真机
验证（含内容不再挤在左侧）待主人在 X5 验收。

### PR-5 全文搜索与阅读状态（Search Engine + Reading State + 书签）

纯 C++ 部分（`search.hpp` + `src/search/`、`reading_state.hpp` + `src/state/`）不依赖
QuickJS，host 单测独立可跑；绑定层（`quickjs/search_binding.cpp` / `quickjs/state_binding.cpp`）
只做参数校验与结果包装。

**偏移语义（三模块同一把尺子）**：`SearchResult.offset`、`ReadingState.textOffset`、
`Bookmark.textOffset` 都是「章节**已解码文本**（UTF-8）内的字节偏移」，恒落在码点
边界——与 `PageInfo.startOffset/endOffset` 完全同语义（见 PR-4 小节），可直接与页
区间比对定位所在页；**不是**原始文件字节偏移（那是 `ChapterInfo.startOffset/
endOffset` 的语义）。`textOffset` 是阅读位置的**唯一事实源**，`pageIndex` 是按当前
分页配置算出的派生缓存。

**搜索（第一阶段朴素字符串搜索，不做索引）**：关键词按字节子串匹配（有效 UTF-8
关键词的匹配必然起止于码点边界，UTF-8 自同步性）；匹配互不重叠、每章内从左到右、
跨章按章节顺序、命中 limit 即停（分章 + 限流保证同步 API 工作量有界）；无匹配返回
空数组（不是错误）；空关键词 / limit<=0 -> `Error code=InvalidArgument`。context
（前后文摘录）默认前后各 32 字节（可配，0 = 该侧不取），预算边界吸附到码点边界
（绝不切开多字节字符），文本首尾自然截断，是纯原文子串（无省略号等附加符号）。

**JS API 形状**（Book 对象方法）：

| API | 返回 | 说明 |
|---|---|---|
| `book.search(keyword, limit?, options?)` | array | `[{chapterIndex, offset, context}]`；limit 缺省 100（正整数限流）；options = `{contextBefore?, contextAfter?}`（字节预算，缺省各 32） |
| `book.getProgress()` | object | `{chapterIndex, pageIndex, textOffset}` |
| `book.setProgress({chapterIndex, textOffset, pageIndex?})` | object | 同上（生效值）；pageIndex 缺省时同章沿用旧值、换章归 0；章节越界/偏移越出章文本/不在码点边界 -> `InvalidArgument` |
| `book.locatePage(pages)` | number | pages = PageInfo[]（形状同 paginateChapter 输出）；用页区间反查当前 textOffset 所在页并写回 pageIndex。**换分页配置后恢复位置 = 重新 paginateChapter + locatePage**；空数组/偏移不在页内 -> `InvalidArgument` |
| `book.addBookmark({chapterIndex, textOffset, note?, createdAt?})` | number | 新书签下标（从 0 起）；位置校验同 setProgress；note/createdAt 是自由文本（createdAt 由 JS 层填，原生只存不解释） |
| `book.removeBookmark(index)` | object | 返回被删书签 `{chapterIndex, textOffset, note, createdAt}`；下标越界 -> `NotFound` |
| `book.listBookmarks()` | array | 书签数组（加入顺序） |
| `book.serializeState()` | string | JSON（version 1，格式见下；**持久化由 JS 存储层负责**，原生只出/入字符串） |
| `book.deserializeState(json)` | undefined | 结构非法 -> `ParseError`；载入前对进度与每个书签做语义复核，非法 -> `InvalidArgument`；两种失败都保持原状态 |

**序列化格式（version 1，定一种）**：

```json
{
  "version": 1,
  "progress": { "chapterIndex": 0, "pageIndex": 0, "textOffset": 0 },
  "bookmarks": [
    { "chapterIndex": 0, "textOffset": 0, "note": "", "createdAt": "" }
  ]
}
```

- 数字均为非负整数（十进制，无小数/指数；解析直接进 uint64，不受 JS Number 2^53
  限制；size_t 字段超平台范围判非法）；字符串按标准 JSON 转义（`\uXXXX` 含代理对），
  UTF-8 原样保留；
- 解析器是手写的最小严格解析器（零依赖）：结构/类型非法 -> `ParseError`，`version`
  缺失或不认识 -> `ParseError`，未知字段忽略（向前兼容），对象后多余内容 ->
  `ParseError`；**非法输入不崩**，失败时原状态保持不变（21 种非法载荷有测试）；
- `deserializeState` 只恢复数据；语义复核（章节存在、偏移在章内且在码点边界）由
  `setProgress`/`addBookmark` 与绑定层加载时做（`validateStateLocation`）。

**校验口径**（与分页绑定一致）：JS 类型不对 -> `TypeError`；数值非法（非整数/负数/
NaN/超范围）-> `Error code=InvalidArgument`；close 后调用 -> `InvalidHandle`。

**测试证据**（host x86_64）：`test_search.cpp` 10 个用例 + `test_reading_state.cpp` 9 个
用例，全仓 **127 个用例全绿、0 失败**（含 PR-1…PR-4 回归）：中/英关键词精确偏移、
多匹配、limit 截断、无匹配、非重叠语义（「aa」在「aaaa」里 0/2）、context 首尾截断
与码点吸附（预算不足一个汉字时退/进到字符边界）、context 可配（0/超长）、整书多章
搜索（章节归属/跨章 limit/close 后 InvalidHandle）、进度保存/恢复、无效偏移/章节号
错误码（越界/越出章文本/码点中间 -> InvalidArgument）、偏移↔页映射逐码点遍历一致、
文末光标归末页、换分页配置后按 textOffset 恢复到新页、书签增删列表、序列化
round-trip（含中文/转义/emoji）、21 种非法 JSON 不崩且原状态不变、未知字段忽略；
ASan+UBSan 全绿（无任何 sanitizer 报告）；绑定层 host 冒烟（`smoke_pr5.mjs`，QuickJS
2020-07-05 真实运行）覆盖 search/进度/locatePage/书签/序列化全部路径与错误码
（TypeError/InvalidArgument/NotFound/ParseError/InvalidHandle）。大文本耗时探针（记录用，
非指标）：约 1MiB 中英混排全量无匹配扫描 ≈ 0.02–0.05 ms、1.7 万处匹配全量收集
≈ 1.7 ms（-O2）/ 7.2 ms（ASan）。设备真机验证待主人在 X5 验收。

### 接口头（后续 PR）

`native/include/readex/` 下接口头已全部落地（`search.hpp` / `reading_state.hpp` 于 PR-5
落地，`pagination.hpp` 为 PR-4，`book.hpp` / `parser.hpp` 为 PR-3，`decoder.hpp` /
`reader.hpp` 为 PR-2），不以空实现冒充完成；后续 PR-6（Vue 层清理）不再新增核心接口头。

### 内存与资源管理规范（PR-1 起生效）

- 可恢复错误一律走 `readex::Result<T>` / `readex::Error`（`native/include/readex/error.hpp`），不把异常当控制流。
- C++ 异常绝不穿过 QuickJS C ABI：binding 层每个 C 入口统一 `try/catch(...)`，经 `readex::currentExceptionToError()` 转成 JS Error。
- 文件句柄/原生资源一律 RAII 管理；不把内部指针/对象地址直接暴露给 JS，JS 侧只拿受管理句柄（PR-3 起）。
- 不一次性把整本书读进内存；大文本用流式/分块接口（PR-2 起）。

## 许可

[GPL-3.0](LICENSE)
