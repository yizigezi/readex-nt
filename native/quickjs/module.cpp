// module.cpp — QuickJS 原生模块注册骨架（设备 jsapi ABI，与 fs_plugin.c 同一套）
//
// 加载 ABI（已实测验证，勿猜勿改）：
//   - .so 导出唯一符号 `void custom_init_jsapis(void)`（visibility default）
//   - 内部调用宿主提供的 registerCModuleLoader(moduleName, loader)，
//     .so 内保持未定义动态符号即可
//   - loader 内 JS_NewCModule + JS_AddModuleExport("default") + JS_SetModuleExport(...)
//   - .so 命名 libjsapi_*.so，打进 AMR 的 libs/ 即被宿主加载
//
// 模块名说明：注册名 "readex"（JS 侧 `import Readex from 'readex'`）。
// 计划书 §四草案写的 '@readex/core' 是草案名，以宿主 loader 注册名为准，
// 偏差说明见 README。
//
// SPDX-License-Identifier: GPL-3.0

#include "quickjs.h"

// bindings.cpp 组装的 API 对象
extern JSValue readex_new_api_object(JSContext* ctx);

// ---- 宿主框架接口（iot-miniapp-sdk jsmodules/JSCModuleExtension.h）----
// 由框架提供，本 .so 内保持未定义动态符号（与 fs 插件一致）。
typedef JSModuleDef* (*LoadCModuleFunction)(JSContext* context, const char* moduleName);
extern "C" void registerCModuleLoader(const char* moduleName, LoadCModuleFunction loader);

// 模块初始化：把 default 导出为 API 对象
static int readex_module_init(JSContext* ctx, JSModuleDef* m)
{
    JSValue api = readex_new_api_object(ctx);
    JS_SetModuleExport(ctx, m, "default", api);
    return 0;
}

// 宿主加载模块 "readex" 时的构建入口
extern "C" JSModuleDef* readex_module_load(JSContext* ctx, const char* module_name)
{
    JSModuleDef* m = JS_NewCModule(ctx, module_name, readex_module_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    return m;
}

// 框架加载 libs/libjsapi_readex.so 时调用的唯一入口
extern "C" __attribute__((visibility("default")))
void custom_init_jsapis(void)
{
    registerCModuleLoader("readex", readex_module_load);
}
