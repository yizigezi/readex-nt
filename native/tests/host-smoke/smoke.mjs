// smoke.mjs — 冒烟脚本：JS 侧调用原生模块，记录结果供 harness 打印
// 覆盖 PR-1 验收面：字符串、数字、对象、参数错误、C++ 异常 -> JS Error
import Readex from 'readex';

// 字符串
globalThis.r_version = Readex.version();

// 数字
globalThis.r_sum = Readex.add(2, 3);

// 对象
const info = Readex.info();
globalThis.r_info_version = info.version;
globalThis.r_info_quickjs = info.quickjs;
globalThis.r_info_platform = info.platform;
globalThis.r_info_buildmode = info.buildMode;

// 缺参 -> TypeError
try {
    Readex.add(1);
    globalThis.r_err_missing = '未抛错(失败)';
} catch (e) {
    globalThis.r_err_missing = (e instanceof TypeError ? 'TypeError' : e.constructor.name) + ': ' + e.message;
}

// 非数字 -> TypeError
try {
    Readex.add('a', 2);
    globalThis.r_err_type = '未抛错(失败)';
} catch (e) {
    globalThis.r_err_type = (e instanceof TypeError ? 'TypeError' : e.constructor.name) + ': ' + e.message;
}

// C++ 异常 -> JS Error
try {
    Readex.fail();
    globalThis.r_err_fail = '未抛错(失败)';
} catch (e) {
    globalThis.r_err_fail = (e instanceof Error ? 'Error' : e.constructor.name) +
        '[code=' + e.code + ']: ' + e.message;
}
