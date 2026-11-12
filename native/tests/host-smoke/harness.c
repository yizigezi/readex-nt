/*
 * harness.c — host 冒烟宿主：模拟设备 jsapi 加载 ABI，跑通 readex 模块的冒烟 API
 *
 * 与设备宿主一致的三个事实：
 *   1. 加载 libs/libjsapi_*.so 后调用其唯一导出符号 custom_init_jsapis
 *   2. .so 内未定义的 registerCModuleLoader 由宿主提供（这里由本文件提供）
 *   3. 模块构建走 JS_NewCModule + JS_AddModuleExport/JS_SetModuleExport
 *
 * 用法（需 QuickJS 2020-07-05 源码，与 vendored 头文件同版本）：
 *   gcc -O2 -rdynamic -I <quickjs-src> \
 *       <quickjs-src>/quickjs.c <quickjs-src>/cutils.c harness.c \
 *       -o smoke_host -ldl -lm -lpthread
 *   ./smoke_host /path/to/libjsapi_readex_host.so smoke.mjs
 *
 * 注意：quickjs.c（MIT）不在本仓库 vendored 范围内，构建前自行获取
 *   https://bellard.org/quickjs/quickjs-2020-07-05.tar.xz
 *
 * SPDX-License-Identifier: GPL-3.0
 */

#include "quickjs.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 与 fs_plugin.c / module.cpp 相同的宿主 ABI ---- */
typedef JSModuleDef* (*LoadCModuleFunction)(JSContext* context, const char* moduleName);

static struct {
    const char* name;
    LoadCModuleFunction loader;
} g_loaders[16];
static int g_loader_count = 0;

/* .so 内的未定义符号 registerCModuleLoader 解析到这里 */
void registerCModuleLoader(const char* moduleName, LoadCModuleFunction loader)
{
    if (g_loader_count < 16) {
        g_loaders[g_loader_count].name = moduleName;
        g_loaders[g_loader_count].loader = loader;
        g_loader_count++;
        printf("[宿主] registerCModuleLoader(\"%s\")\n", moduleName);
    }
}

static JSModuleDef* harness_module_loader(JSContext* ctx, const char* module_name,
                                          void* opaque)
{
    (void)opaque;
    for (int i = 0; i < g_loader_count; i++) {
        if (strcmp(g_loaders[i].name, module_name) == 0)
            return g_loaders[i].loader(ctx, module_name);
    }
    return NULL;
}

static char* read_file(const char* path, size_t* out_len)
{
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[n] = 0;
    *out_len = n;
    return buf;
}

static void dump_prop(JSContext* ctx, JSValueConst global, const char* key)
{
    JSValue v = JS_GetPropertyStr(ctx, global, key);
    const char* s = JS_ToCString(ctx, v);
    printf("  %s = %s\n", key, s ? s : "(无法转换)");
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, v);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "用法: %s <libjsapi_*.so> <smoke.mjs> [book_path]\n", argv[0]);
        return 2;
    }

    /* 1. 加载 .so 并执行 custom_init_jsapis（设备宿主的加载路径） */
    void* so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!so) {
        fprintf(stderr, "dlopen 失败: %s\n", dlerror());
        return 2;
    }
    void (*init)(void) = (void (*)(void))dlsym(so, "custom_init_jsapis");
    if (!init) {
        fprintf(stderr, "dlsym custom_init_jsapis 失败\n");
        return 2;
    }
    init();

    /* 2. 建 QuickJS 运行时，模块加载委托给注册的 C loader */
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    JS_SetModuleLoaderFunc(rt, NULL, harness_module_loader, NULL);

    /* 可选第 3 参数作为全局字符串 SMOKE_ARG 传给脚本（如测试书路径） */
    {
        JSValue g = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, g, "SMOKE_ARG",
                          JS_NewString(ctx, argc > 3 ? argv[3] : ""));
        JS_FreeValue(ctx, g);
    }

    /* 3. 跑冒烟脚本（ES 模块） */
    size_t len = 0;
    char* code = read_file(argv[2], &len);
    if (!code) {
        fprintf(stderr, "无法读取 %s\n", argv[2]);
        return 2;
    }
    JSValue ret = JS_Eval(ctx, code, len, argv[2], JS_EVAL_TYPE_MODULE);
    free(code);
    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(ctx);
        const char* s = JS_ToCString(ctx, exc);
        printf("冒烟脚本执行抛出异常: %s\n", s ? s : "?");
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, ret);
        return 1;
    }
    JS_FreeValue(ctx, ret);

    /* 4. 打印脚本记录的结果 */
    JSValue global = JS_GetGlobalObject(ctx);
    const char* keys[] = { "r_version", "r_sum", "r_info_version", "r_info_quickjs",
                           "r_info_platform", "r_info_buildmode",
                           "r_err_missing", "r_err_type", "r_err_fail",
                           "r_smoke_ok", "r_smoke_fails",
                           "r_info", "r_chapters", "r_chapter1", "r_bookerrs",
                           "r_multi", "r_closeall" };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        dump_prop(ctx, global, keys[i]);

    /* 5. PR-3：强制 GC 后验证 class finalizer 清理句柄表
     *    （脚本里故意丢弃的 Book 对象应被回收，closeAllBooks 只剩显式保留的） */
    JS_RunGC(rt);
    static const char kGcCheck[] =
        "if (globalThis.ReadexApi) "
        "{ globalThis.r_after_gc = String(ReadexApi.closeAllBooks()); }";
    JSValue gc_ret = JS_Eval(ctx, kGcCheck, (size_t)strlen(kGcCheck),
                             "<gc-check>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(gc_ret)) {
        JSValue exc = JS_GetException(ctx);
        const char* s = JS_ToCString(ctx, exc);
        printf("GC 后检查脚本抛出异常: %s\n", s ? s : "?");
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, gc_ret);
        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 1;
    }
    JS_FreeValue(ctx, gc_ret);
    dump_prop(ctx, global, "r_after_gc");
    JS_FreeValue(ctx, global);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    printf("冒烟宿主结束（未打印项即脚本内断言失败）\n");
    return 0;
}
