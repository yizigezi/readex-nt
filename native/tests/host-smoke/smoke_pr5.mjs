// smoke_pr5.mjs — PR-5 绑定冒烟：book.search + 阅读状态（进度/偏移↔页映射/
// 书签/序列化）+ 参数校验与错误码
//
// 断言失败会 throw（harness 打印异常并退出非 0）。偏移是章内已解码文本的
// UTF-8 字节偏移（见 search.hpp / reading_state.hpp 头注释）。测试书由
// SMOKE_ARG 传入（三章，正文含「关键词」）。
import Readex from 'readex';

globalThis.ReadexApi = Readex; // 供 harness 的 GC 后检查使用

function check(cond, name) {
    if (!cond) throw new Error('断言失败: ' + name);
}

const path = globalThis.SMOKE_ARG;
if (!path) throw new Error('缺少 SMOKE_ARG（测试书路径）');
const book = Readex.openBook(path);

// ---- book.search：中文 / 多匹配 / limit / 无匹配 ----
const rs = book.search('关键词');
check(Array.isArray(rs) && rs.length === 3, 'search: 3 处命中');
check(rs[0].chapterIndex === 0 && rs[1].chapterIndex === 1 &&
    rs[2].chapterIndex === 1, 'search: 章节归属 0/1/1');
for (const r of rs) {
    check(typeof r.offset === 'number' && Number.isInteger(r.offset) &&
        r.offset >= 0, 'search: offset 形状');
    check(r.context.indexOf('关键词') >= 0, 'search: context 含关键词');
}
// context 长度可配（双 0 -> 只剩关键词本身）
const rs0 = book.search('关键词', 10, { contextBefore: 0, contextAfter: 0 });
check(rs0.length === 3 && rs0[0].context === '关键词', 'search: context 可配');
// limit 限流
check(book.search('关键词', 2).length === 2, 'search: limit 截断');
check(book.search('绝不存在', 10).length === 0, 'search: 无匹配 -> 空');
globalThis.r_search = rs.length + ' hits, ctx=' + rs0[0].context;

// ---- search 参数校验 ----
let t;
t = ''; try { book.search(); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : (e.code || 'Error'); }
check(t === 'TypeError', 'search 缺关键词应抛 TypeError');
t = ''; try { book.search(42); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'search 关键词非字符串应抛 TypeError');
t = ''; try { book.search('x', 0); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'search limit=0 应报 InvalidArgument');
t = ''; try { book.search('x', 1.5); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'search limit 非整数应报 InvalidArgument');
t = ''; try { book.search('x', 'y'); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'search limit 非数字应抛 TypeError');
t = ''; try { book.search('x', 10, 42); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'search options 非对象应抛 TypeError');
t = ''; try { book.search('x', 10, { contextBefore: -1 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'contextBefore 负数应报 InvalidArgument');

// ---- 阅读状态：进度 ----
const p0 = book.getProgress();
check(p0.chapterIndex === 0 && p0.pageIndex === 0 && p0.textOffset === 0,
    'progress: 默认全 0');
const p1 = book.setProgress({ chapterIndex: 1, textOffset: 10 });
check(p1.chapterIndex === 1 && p1.textOffset === 10, 'progress: set/get');
check(book.getProgress().chapterIndex === 1, 'progress: 持久于 Book');
// pageIndex 缺省：换章归 0；显式传入原样记录
check(book.getProgress().pageIndex === 0, 'progress: 换章 pageIndex 归 0');
const p2 = book.setProgress({ chapterIndex: 0, textOffset: 0, pageIndex: 2 });
check(p2.pageIndex === 2, 'progress: pageIndex 显式传入');

// ---- progress 参数校验（无效偏移/章节号 -> InvalidArgument，不崩）----
t = ''; try { book.setProgress({ chapterIndex: 99, textOffset: 0 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', '章节越界应报 InvalidArgument');
t = ''; try { book.setProgress({ chapterIndex: 0, textOffset: 1 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', '偏移不在码点边界应报 InvalidArgument');
t = ''; try { book.setProgress({ chapterIndex: 0, textOffset: 99999 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', '偏移越界应报 InvalidArgument');
t = ''; try { book.setProgress('x'); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'setProgress 非对象应抛 TypeError');
t = ''; try { book.setProgress({ textOffset: 0 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', '缺 chapterIndex 应报 InvalidArgument');
t = ''; try { book.setProgress({ chapterIndex: -1, textOffset: 0 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'chapterIndex 负数应报 InvalidArgument');
t = ''; try { book.setProgress({ chapterIndex: 1.5, textOffset: 0 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'chapterIndex 非整数应报 InvalidArgument');
globalThis.r_progress = 'ok';

// ---- 偏移↔页映射（locatePage）----
const cfg = { width: 120, height: 50, fontSize: 10, lineHeight: 1.0 };
book.setProgress({ chapterIndex: 0, textOffset: 10 });
const pages0 = book.paginateChapter(0, cfg);
const pi = book.locatePage(pages0);
check(typeof pi === 'number' && pi >= 0 && pi < pages0.length,
    'locatePage: 页号在范围内');
check(book.getProgress().pageIndex === pi, 'locatePage: 写回 pageIndex');
const pg = pages0[pi];
check(pg.startOffset <= 10 && 10 < pg.endOffset, 'locatePage: 页区间含偏移');
// 换分页配置后按 textOffset 恢复到新页
const pages1 = book.paginateChapter(0, { width: 300, height: 50, fontSize: 10, lineHeight: 1.0 });
const pi1 = book.locatePage(pages1);
check(book.getProgress().textOffset === 10, 'locatePage: textOffset 是事实源');
check(pages1[pi1].startOffset <= 10 && 10 < pages1[pi1].endOffset,
    'locatePage: 新页含偏移');
t = ''; try { book.locatePage([]); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'locatePage 空页表应报 InvalidArgument');
t = ''; try { book.locatePage('x'); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'locatePage 非数组应抛 TypeError');
t = ''; try { book.locatePage([{ startOffset: 100, endOffset: 200 }]); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'locatePage 偏移不在页内应报 InvalidArgument');

// ---- 书签 ----
const i0 = book.addBookmark({ chapterIndex: 0, textOffset: 0, note: '第一处', createdAt: '2026-11-13T01:40:00+08:00' });
const i1 = book.addBookmark({ chapterIndex: 1, textOffset: 10 });
check(i0 === 0 && i1 === 1, 'addBookmark: 下标递增');
let list = book.listBookmarks();
check(list.length === 2, 'listBookmarks: 2 个');
check(list[0].note === '第一处' && list[0].createdAt === '2026-11-13T01:40:00+08:00',
    'listBookmarks: 字段完整');
check(list[1].note === '' && list[1].createdAt === '', 'listBookmarks: 可选字段缺省为空');
const gone = book.removeBookmark(0);
check(gone.note === '第一处' && gone.chapterIndex === 0 && gone.textOffset === 0,
    'removeBookmark: 返回被删书签');
list = book.listBookmarks();
check(list.length === 1 && list[0].chapterIndex === 1, 'removeBookmark: 列表收缩');
t = ''; try { book.removeBookmark(5); } catch (e) { t = e.code || ''; }
check(t === 'NotFound', 'removeBookmark 越界应报 NotFound');
t = ''; try { book.addBookmark({ chapterIndex: 99, textOffset: 0 }); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', 'addBookmark 无效位置应报 InvalidArgument');
t = ''; try { book.addBookmark('x'); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'addBookmark 非对象应抛 TypeError');
t = ''; try { book.removeBookmark('x'); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'removeBookmark 非数字应抛 TypeError');
globalThis.r_bm = 'ok';

// ---- 序列化 / 反序列化（JSON version 1，持久化由 JS 存储层做）----
const json = book.serializeState();
check(typeof json === 'string' && JSON.parse(json).version === 1,
    'serializeState: JSON version 1');
check(JSON.parse(json).bookmarks.length === 1, 'serializeState: 含书签');
// 换个状态再载入：round-trip 恢复
book.setProgress({ chapterIndex: 1, textOffset: 10, pageIndex: 7 });
book.deserializeState(json);
const pr = book.getProgress();
check(pr.chapterIndex === 0 && pr.textOffset === 10, 'deserializeState: 进度恢复');
check(book.listBookmarks().length === 1, 'deserializeState: 书签恢复');
t = ''; try { book.deserializeState('not json'); } catch (e) { t = e.code || ''; }
check(t === 'ParseError', '非法 JSON 应报 ParseError');
t = ''; try { book.deserializeState('{"version":2}'); } catch (e) { t = e.code || ''; }
check(t === 'ParseError', '不认识的版本应报 ParseError');
t = ''; try { book.deserializeState('{"version":1,"progress":{"chapterIndex":99,"textOffset":0}}'); } catch (e) { t = e.code || ''; }
check(t === 'InvalidArgument', '语义越界载荷应报 InvalidArgument');
t = ''; try { book.deserializeState(42); } catch (e) { t = (e instanceof TypeError) ? 'TypeError' : 'other'; }
check(t === 'TypeError', 'deserializeState 非字符串应抛 TypeError');
// 失败载入不破坏原状态
check(book.getProgress().chapterIndex === 0, '失败载入后原状态不变');
globalThis.r_state = 'ok';

// ---- close 后统一 InvalidHandle ----
book.close();
t = ''; try { book.search('x'); } catch (e) { t = e.code || ''; }
check(t === 'InvalidHandle', 'close 后 search 应报 InvalidHandle');
t = ''; try { book.getProgress(); } catch (e) { t = e.code || ''; }
check(t === 'InvalidHandle', 'close 后 getProgress 应报 InvalidHandle');

Readex.closeAllBooks();
globalThis.r_smoke_ok = 'PASS';
globalThis.r_smoke_fails = '(none)';
