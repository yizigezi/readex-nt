# Readex NT

面向有道词典笔 X5 / PenOS 的第三方文本阅读器。UI 使用 Falcon + Vue，阅读核心使用独立 C++ 模块，通过设备提供的 QuickJS jsapi ABI 接入。

## 项目状态

- UI：Material 3 Expressive 风格文件库与阅读界面。
- Native Core：文件读取、编码解码、章节解析、分页、搜索、阅读进度和书签。
- 平台：ARMv7 EABI5 hard-float，目标 glibc 2.23，QuickJS 2020-07-05。
- 验证：GitHub Actions 执行 host 原生单元测试和 Falcon AMR 构建；CI 成功不代表已完成 X5 真机验收。

## 开发与构建

```bash
npm install
npm run check
npm run build:dev
npm run build:prod
```

连接设备后可使用：

```bash
./install.sh
```

### Native Core

Host 单元测试：

```bash
cmake -S native -B build-host \
  -DREADEX_BUILD_QUICKJS_MODULE=OFF \
  -DREADEX_BUILD_TESTS=ON
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

交叉编译并自检设备插件：

```bash
tools/build-core.sh
tools/build-fs-plugin.sh
```

只检查已提交的设备库：

```bash
tools/build-core.sh --check
tools/build-fs-plugin.sh --check
```

构建核心插件优先使用 Zig，并检查 ARM hard-float、宿主入口符号、动态依赖和 GLIBC 符号版本。不要绕过 ELF 自检将不兼容的 .so 安装到设备。

## 目录结构

```text
src/                         Falcon / Vue 应用
  app.js                     应用生命周期
  base-page.js               页面基类
  pages/index/               文件库与书籍入口
  pages/reader/              阅读页面
  styles/                    全局样式
  utils/                     文件系统、日志、书目和会话
native/
  include/readex/             C++ 核心公共接口
  src/                        与 QuickJS 解耦的核心实现
  quickjs/                    QuickJS 模块入口与 JS 绑定
  tests/                      原生单测和 QuickJS host 冒烟测试
  fs_plugin.c                 独立文件系统 jsapi 插件
libs/                         随 AMR 打包的原生 .so
tools/                        交叉编译和 ELF 自检脚本
docs/                         架构与设计文档
.github/workflows/            CI 工作流
```

## JS API

模块名为 `readex`：

```js
import Readex from 'readex'

const book = Readex.openBook('/path/to/book.txt')
const info = book.getInfo()
const chapters = book.getChapters()
const chapter = book.getChapter(0)
const pages = Readex.paginate(chapter.text, {
  width: 400,
  height: 600
})
book.close()
```

Book 还提供全文搜索、进度、页定位、书签和状态序列化接口。详细参数、错误语义和偏移约定以接口实现及测试为准。

**偏移约定非常重要：**
- `ChapterInfo.startOffset/endOffset` 是原始文件字节偏移。
- 分页、搜索、阅读进度和书签中的 offset 是章节解码后 UTF-8 文本的字节偏移。
- 两者不可直接混用；JS 字符串下标也不等于 UTF-8 字节偏移。

## 文件系统

UI 统一通过 `src/utils/FileSystem/FileSystem.js` 访问文件。适配层优先动态加载设备 `fs` jsapi；接口不可用时回退到 `global.execShell`。不要在页面中直接拼接 shell 命令。

## 日志与故障排查

- UI：`/userdisk/readex/app.ui.log`
- Native：`/userdisk/readex/app.native.log`
- Native 日志另有备用写入路径，详见 `native/quickjs/native_logger.hpp`。
- 构建页面显示的 commit/run 信息由 CI 生成，原生库构建时间也会写入构建元数据。

若没有看到 `openBook` 日志，按 `library.init` → `library.register` → `module.load` → `api.export.openBook` → `openBook.entry` → `openBook.core.begin` 顺序定位，不要仅凭单条日志缺失判断核心函数是否执行。

## 详细文档

- [项目架构与数据流](docs/architecture.md)
- [原生异步 I/O 设计](docs/native-async-io.md)
- [Native Core 原生化重构计划](docs/Readex%20Native%20Core%20原生化重构计划.docx)

## 许可

本项目按 [GPL-3.0](LICENSE) 发布。引入第三方代码或资源时，需先核对其许可证和再分发条件。
