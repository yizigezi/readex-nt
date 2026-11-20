/**
 * build:web 预览用 readex mock（真机走 libjsapi_readex.so）。
 * API 面与 native 模块对齐：openBook/getInfo/getChapters/getChapter/paginateChapter/close。
 */
const CHAPTERS = [
  { title: '第一章 开始', start: 0 },
  { title: '第二章 继续', start: 1 }
];

function makePage(text) {
  return { text, startOffset: 0, endOffset: text.length };
}

export default {
  openBook(path) {
    return {
      getInfo() {
        return { title: '示例书籍', path, totalChapters: CHAPTERS.length };
      },
      getChapters() {
        return CHAPTERS.slice();
      },
      getChapter(index) {
        return { title: (CHAPTERS[index] || CHAPTERS[0]).title };
      },
      paginateChapter(index, config) {
        const demo =
          '这是一段用于预览排版的示例文本。词典笔的屏幕是横向的，' +
          '所以阅读区保持固定几何，分页由原生模块计算。\n' +
          '字号 ' + config.fontSize + 'px，行距 ' + config.lineHeight + '×。';
        return [makePage(demo), makePage('第二页示例内容。')];
      },
      close() {}
    };
  }
};
