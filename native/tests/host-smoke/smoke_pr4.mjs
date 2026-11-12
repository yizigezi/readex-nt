// smoke_pr4.mjs — PR-4 绑定冒烟：Readex.paginate + book.paginateChapter +
// 配置校验策略 + 偏移语义（UTF-8 字节偏移、无缝拼接）
//
// 断言失败会 throw（harness 打印异常并退出非 0）。偏移是 content 字符串内的
// UTF-8 字节偏移（见 pagination.hpp 头注释）：纯 ASCII 素材里 == JS 下标，
// 可直接 substring 验证拼接性质；中文素材用 utf8* 辅助函数换算验证。
import Readex from 'readex';

globalThis.ReadexApi = Readex; // 供 harness 的 GC 后检查使用

function check(cond, name) {
    if (!cond) throw new Error('断言失败: ' + name);
}

// ---- UTF-8 辅助（JS 字符串下标 <-> 字节偏移换算）----
function utf8Len(str) {
    let n = 0;
    for (let i = 0; i < str.length; i++) {
        const c = str.charCodeAt(i);
        if (c < 0x80) n += 1;
        else if (c < 0x800) n += 2;
        else if (c >= 0xD800 && c < 0xDC00 && i + 1 < str.length) { n += 4; i++; }
        else n += 3;
    }
    return n;
}

function jsIndexForByte(str, byteOff) {
    let bytes = 0, i = 0;
    while (i < str.length && bytes < byteOff) {
        const c = str.charCodeAt(i);
        if (c < 0x80) bytes += 1;
        else if (c < 0x800) bytes += 2;
        else if (c >= 0xD800 && c < 0xDC00 && i + 1 < str.length) { bytes += 4; i++; }
        else bytes += 3;
        i++;
    }
    return i;
}

// 各页区间无缝拼接 == 原文（在 JS 字符串层验证）
function checkPages(text, pages, name) {
    check(pages.length >= 1, name + ': 页数 >= 1');
    check(pages[0].startOffset === 0, name + ': 首页起点 0');
    for (let i = 0; i < pages.length; i++) {
        check(pages[i].pageIndex === i, name + ': pageIndex 递增');
        if (i > 0) check(pages[i].startOffset === pages[i - 1].endOffset,
            name + ': 页区间无缝');
    }
    check(pages[pages.length - 1].endOffset === utf8Len(text),
        name + ': 末页终点 == 文本字节长');
    let joined = '';
    for (const p of pages) {
        joined += text.substring(jsIndexForByte(text, p.startOffset),
            jsIndexForByte(text, p.endOffset));
    }
    check(joined === text, name + ': 拼接 == 原文（不丢字、不重复）');
}

const cfg = { width: 120, height: 50, fontSize: 10, lineHeight: 1.0 };

// ---- 纯 ASCII：偏移 == JS 下标，直接 substring 验证 ----
const asciiText = 'Hello world, pagination engine! 12345 67890 abcdef ghijkl';
const asciiPages = Readex.paginate(asciiText, cfg);
checkPages(asciiText, asciiPages, 'ascii');
globalThis.r_ascii = asciiPages.length + ' pages';

// ---- 中英混排 + 中文标点 ----
const cnText = '他说：「今天天气很好。」然后我们就去公园了——真的很不错……The end! 42';
const cnPages = Readex.paginate(cnText, cfg);
checkPages(cnText, cnPages, 'cn-mixed');
globalThis.r_cn = cnPages.length + ' pages';

// ---- 稳定性：同输入两次结果一致 ----
const again = Readex.paginate(cnText, cfg);
check(JSON.stringify(again) === JSON.stringify(cnPages), '分页结果稳定');

// ---- 标点避头尾：句号不落行首 ----
// 行宽 35 装 3 个 10px 字："甲乙丙。" 应断在「甲乙 | 丙。」而非「甲乙丙 | 。」
const punctPages = Readex.paginate('甲乙丙。', { width: 35, height: 10, fontSize: 10, lineHeight: 1 });
check(punctPages.length === 2, '避头尾: 2 页');
check(punctPages[0].endOffset === 6, '避头尾: 。不落行首（断在 6 字节处）');
check(punctPages[1].startOffset === 6, '避头尾: 次页起点 6');
globalThis.r_punct = punctPages[0].endOffset + '/' + punctPages[1].startOffset;

// ---- 配置校验策略 ----
let t;
t = ''; try { Readex.paginate('x'); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : (e.code || 'Error'); }
check(t === 'TypeError', 'paginate 缺 config 应抛 TypeError');
t = ''; try { Readex.paginate('x', 42); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'config 非对象应抛 TypeError');
t = ''; try { Readex.paginate(42, cfg); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'content 非字符串应抛 TypeError');
t = ''; try { Readex.paginate('x', { height: 50 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', '缺 width 应报 InvalidArgument（必填字段）');
t = ''; try { Readex.paginate('x', { width: 50 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', '缺 height 应报 InvalidArgument（必填字段）');
t = ''; try { Readex.paginate('x', { width: 0, height: 50 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'width=0 应报 InvalidArgument');
t = ''; try { Readex.paginate('x', { width: 50, height: 50, fontSize: -1 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'fontSize<0 应报 InvalidArgument');
t = ''; try { Readex.paginate('x', { width: 'abc', height: 50 }); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'width 类型错应抛 TypeError');
// 缺省字段用默认值：只给 width/height 也能跑（fontSize=20 等走默认）
const defPages = Readex.paginate('甲乙丙丁', { width: 50, height: 50 });
check(Array.isArray(defPages) && defPages.length >= 1, '缺省字段走默认值');
// 未知字段忽略
const unkPages = Readex.paginate('甲乙丙丁', { width: 50, height: 50, whatever: 'x' });
check(unkPages.length === defPages.length, '未知字段忽略');
globalThis.r_cfg = 'ok';

// ---- book.paginateChapter（便捷形状）----
const path = globalThis.SMOKE_ARG;
if (!path) throw new Error('缺少 SMOKE_ARG（测试书路径）');
const book = Readex.openBook(path);
const ch0 = book.getChapter(0);
const chPages = book.paginateChapter(0, cfg);
checkPages(ch0.text, chPages, 'paginateChapter');
globalThis.r_chpages = chPages.length + ' pages';

// 顶层与便捷形状结果一致
const direct = Readex.paginate(ch0.text, cfg);
check(JSON.stringify(direct) === JSON.stringify(chPages), '两形状结果一致');

// ---- 错误路径 ----
t = ''; try { book.paginateChapter(99, cfg); } catch (e) { t = e.code || ''; }
check(t === 'NotFound', 'paginateChapter 越界应报 NotFound');
t = ''; try { book.paginateChapter('x', cfg); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'paginateChapter 非数字应抛 TypeError');
t = ''; try { book.paginateChapter(0, { width: 50 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'paginateChapter 缺 height 应报 InvalidArgument');

book.close();
t = ''; try { book.paginateChapter(0, cfg); } catch (e) { t = e.code || ''; }
check(t === 'InvalidHandle', 'close 后 paginateChapter 应报 InvalidHandle');
globalThis.r_bookerrs = 'ok';

Readex.closeAllBooks();
globalThis.r_smoke_ok = 'PASS';
globalThis.r_smoke_fails = '(none)';
