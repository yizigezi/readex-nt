# 项目架构

本文描述 `ui/material3-expressive-x5` 当前代码的实际职责边界。它是维护文档，不代表设备端验收已经完成。

## 总体结构

```text
src/                         Falcon / Vue MiniApp
  app.js                     应用生命周期与全局环境补齐
  base-page.js               页面基类与页面资源清理
  pages/
    index/                   文件库、目录浏览和书籍入口
    reader/                  阅读会话、章节/分页展示、进度交互
  styles/                    全局样式与 Material 色板
  utils/
    FileSystem/              文件系统能力探测与统一适配
    Logger.js                UI 诊断日志
    BookLibrary.js           设置、书目、最近阅读与进度持久化
    BookSession.js            页面间当前书籍路径
    TextOffset.js              UTF-8 字节偏移与 JS 字符串下标游标
    BuildInfo.js              构建元数据（CI 生成，源码含本地回退）

native/                      C++ 核心与 QuickJS 适配
  include/readex/             与 QuickJS 无关的公共接口
  src/                        核心实现，按领域拆分
    core/                     版本、平台和基础能力
    reader/                   RAII 文件读取与偏移管理
    decoder/                  编码识别及增量解码
    parser/                   章节识别与原始文件字节区间
    book/                     Book 生命周期和受管理句柄
    pagination/               分页算法与文本偏移区间
    search/                   全文搜索
    state/                    进度、书签和状态序列化
  quickjs/                    QuickJS 模块入口、JS/C++ 参数转换和对象绑定
  tests/                      host 单元测试及 QuickJS 冒烟宿主
  fs_plugin.c                 独立 fs jsapi 插件

libs/                         随 AMR 打包的设备端原生插件
tools/                        原生交叉编译、自检和辅助脚本
.github/workflows/            CI 构建、测试、元数据和 AMR artifact
docs/                         架构、接口和实施记录
```

## 依赖方向

- Vue 页面依赖 `src/utils`，不直接访问原生文件句柄或 C++ 实现。
- `FileSystem.js` 是文件访问的唯一 JS 适配入口：优先动态加载 `fs` 模块，接口不完整或模块不可用时回退到 `execShell`。
- `BookLibrary.js` 负责应用层持久化；原生 ReadingState 只负责状态数据结构与校验/序列化，不代替 JS 存储层。
- `native/src` 只依赖标准 C/C++ 与项目公共头文件，不依赖 QuickJS。
- `native/quickjs` 是薄适配层：校验 JS 参数、调用核心、包装 JS 值，并将 C++ 异常转换为 JS Error。
- `native/fs_plugin.c` 与 `libjsapi_readex.so` 是两个独立插件，分别提供文件系统兼容 API 和阅读核心 API。

## 阅读数据流

1. 首页通过 `FileSystem.js` 浏览文件，用户选择书籍后将路径写入 `BookSession` 并进入阅读页。
2. 阅读页动态加载 `readex` 模块，调用 `Readex.openBook(path)` 创建受管理 Book 句柄。
3. Native Reader 按需读取文件，Decoder 识别/解码文本，Parser 生成章节及原始文件字节区间。
4. 阅读页获取章节文本，再调用分页 API。分页、搜索、进度和书签使用**章节解码后 UTF-8 文本内的字节偏移**；章节元数据的 start/end 则是**原始文件字节偏移**，两种偏移不可混用。
5. JS 应用层把进度与书目写入 `/userdisk/readex`；离开页面时关闭 Book 句柄，避免资源长期占用。

## 原生模块加载链

`libs/libjsapi_readex.so` 导出唯一宿主入口 `custom_init_jsapis`。入口注册模块名 `readex`，宿主随后通过 QuickJS loader 创建 ES 模块，导出 default API 对象。JS 侧使用：

```js
import Readex from 'readex'
```

诊断日志按阶段区分：

- `library.init.*`：宿主是否进入 .so 初始化入口。
- `library.register.*`：是否注册模块 loader。
- `module.load.*`：宿主是否请求加载 readex 模块。
- `api.create.begin` / `api.export.openBook`：API 对象是否创建并导出 openBook。
- `openBook.entry`：JS 是否真正进入 native openBook 绑定函数。
- `openBook.core.begin`：是否进入 C++ 核心打开流程。

Native 日志优先写入 `/userdisk/readex/app.native.log`，并有备用路径；UI 日志写入 `/userdisk/readex/app.ui.log`。日志缺失只能说明对应阶段没有留下记录，排查时还必须核对设备安装包版本和实际加载的 .so。

## 构建与验证

- `npm run check`：Falcon/Vue 项目静态检查。
- `npm run build:prod`：生产 AMR 构建。
- `cmake -S native -B build-host -DREADEX_BUILD_QUICKJS_MODULE=OFF -DREADEX_BUILD_TESTS=ON`：配置 host 核心测试。
- `cmake --build build-host --parallel` 与 `ctest --test-dir build-host --output-on-failure`：编译并运行核心单测。
- `tools/build-core.sh`：交叉编译阅读核心插件并执行 ELF 自检。
- `tools/build-fs-plugin.sh`：交叉编译 fs 插件并执行 ELF 自检。
- GitHub Actions 同时执行 host 核心测试与设备 AMR 构建，并上传 AMR 和构建元数据 artifact。

CI 通过不等于 X5 真机验收通过。设备侧仍需确认插件实际加载、文件系统能力、字体测量/分页效果、长文本性能及阅读进度恢复。

## 修改约定

1. 新增核心业务逻辑优先放入 `native/include/readex` 与 `native/src`，不要塞进 QuickJS binding。
2. QuickJS binding 不保存裸 C++ 指针到 JS；跨 JS 生命周期的资源必须经受管理句柄表。
3. 页面不直接拼 shell 命令；文件访问统一经 `FileSystem.js`。
4. 修改 JS API、偏移语义、持久化格式或设备 ABI 时，同步更新 README/本文件和对应测试。
5. 不因“清理”删除未知用途的设备兼容逻辑、测试 fixture 或构建产物；先确认引用关系和真机影响。
