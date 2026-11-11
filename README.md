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
│   ├── fs_plugin.c       # fs 原生模块（QuickJS C 模块，导出 custom_init_jsapis）
│   └── quickjs/          # QuickJS 2020-07-05 头文件（原生插件编译用）
├── libs/                 # 原生 .so 插件（随 AMR 打包）
│   └── libjsapi_fs.so    # fs 插件产物（armhf / hard-float / glibc 2.23）
├── tools/
│   └── build-fs-plugin.sh  # fs 插件编译 + ELF 自检（--check 仅自检）
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

## 许可

[GPL-3.0](LICENSE)
