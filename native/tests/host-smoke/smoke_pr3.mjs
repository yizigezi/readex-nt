// smoke_pr3.mjs — PR-3 绑定冒烟：Book 对象生命周期 + 章节 API + 错误路径
//
// 断言失败会 throw（harness 打印异常并退出非 0）；通过时全局记录结果供
// harness 打印。GC 清理验证由 harness 在脚本结束后强制 JS_RunGC 并检查
// r_after_gc（应为 "1"：只剩显式保留的那本书）。
import Readex from 'readex';

globalThis.ReadexApi = Readex; // 供 harness 的 GC 后检查使用

const path = globalThis.SMOKE_ARG;
if (!path) throw new Error('缺少 SMOKE_ARG（测试书路径）');

function check(cond, name) {
    if (!cond) throw new Error('断言失败: ' + name);
}

// ---- 参数校验（TypeError / 错误码）----
let t;
t = ''; try { Readex.openBook(); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : e.constructor.name; }
check(t === 'TypeError', 'openBook 缺参应抛 TypeError');
t = ''; try { Readex.openBook(123); } catch (e) { t = (e instanceof TypeError && e.message.indexOf('number') >= 0) ? 'TypeError' : '其他'; }
check(t === 'TypeError', 'openBook 非字符串应抛 TypeError（message 带实际类型）');
t = ''; try { Readex.openBook('/missing/nope.txt'); } catch (e) { t = e.code || ''; }
check(t === 'NotFound', 'openBook 不存在文件应报 NotFound');

// ---- 打开 + 元信息 ----
const book = Readex.openBook(path);
const info = book.getInfo();
check(info.title === 'smoke_book', 'info.title');
check(info.encoding === 'utf-8', 'info.encoding');
check(info.chapterCount === 3, 'info.chapterCount');
check(info.sizeBytes > 0, 'info.sizeBytes');
globalThis.r_info = info.title + '/' + info.encoding + '/' + info.chapterCount;

// ---- 章节目录 + 偏移 ----
const chapters = book.getChapters();
check(chapters.length === 3, 'chapters.length');
check(chapters[0].title === 'smoke_book', 'chapters[0].title（正文前章）');
check(chapters[1].title === '第一章 初见', 'chapters[1].title');
check(chapters[2].title === '第二章 再见', 'chapters[2].title');
check(chapters[0].startOffset === 0, 'chapters[0].startOffset');
check(chapters[2].endOffset === info.sizeBytes, 'chapters[2].endOffset');
globalThis.r_chapters = chapters.map(c => c.title).join(' | ');

// ---- 章节正文 ----
const c1 = book.getChapter(1);
check(c1.index === 1, 'getChapter(1).index');
check(c1.title === '第一章 初见', 'getChapter(1).title');
check(c1.text === '第一章 初见\n正文一甲乙。\n', 'getChapter(1).text == 原文');
globalThis.r_chapter1 = JSON.stringify(c1.text);

// ---- 错误路径 ----
t = ''; try { book.getChapter(99); } catch (e) { t = e.code || ''; }
check(t === 'NotFound', 'getChapter 越界应报 NotFound');
t = ''; try { book.getChapter('x'); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : e.constructor.name; }
check(t === 'TypeError', 'getChapter 非数字应抛 TypeError');
t = ''; try { book.getChapter(-1); } catch (e) { t = (e instanceof RangeError) ? 'RangeError' : e.constructor.name; }
check(t === 'RangeError', 'getChapter 负数应抛 RangeError');
globalThis.r_bookerrs = 'ok';

// ---- 多书并行不串扰 ----
const book2 = Readex.openBook(path);
check(book2.getChapter(1).text === c1.text, '多书并行内容一致');
check(book2.getInfo().title === info.title, '多书并行元信息');

// ---- close 后访问 + 重复 close ----
book2.close();
t = ''; try { book2.getInfo(); } catch (e) { t = e.code || ''; }
check(t === 'InvalidHandle', 'close 后 getInfo 应报 InvalidHandle');
t = ''; try { book2.getChapter(0); } catch (e) { t = e.code || ''; }
check(t === 'InvalidHandle', 'close 后 getChapter 应报 InvalidHandle');
book2.close(); // 重复关闭安全
globalThis.r_multi = 'ok';

// ---- closeAllBooks 显式清理 ----
check(Readex.closeAllBooks() === 1, 'closeAllBooks 应关闭 1 本（book）');
t = ''; try { book.getInfo(); } catch (e) { t = e.code || ''; }
check(t === 'InvalidHandle', 'closeAllBooks 后访问应报 InvalidHandle');
globalThis.r_closeall = '1';

// ---- 丢弃两本书（GC 后由 finalizer 清理），保留一本 ----
function dropBook() {
    const b = Readex.openBook(path);
    b.getChapter(0); // 用一下
    // b 出作用域即不可达
}
dropBook();
dropBook();
globalThis.r_kept = Readex.openBook(path); // 显式保留，GC 后应是唯一存活

globalThis.r_smoke_ok = 'PASS';
globalThis.r_smoke_fails = '(none)';
