/**
 * build:web 预览用 global mock（utils/http.js 的 execShell 兜底链会 import('global')）。
 * web 预览下没有 shell，返回不可用，让请求链自然降级报错。
 */
export default {
  async execShell() {
    throw new Error('web 预览无 execShell');
  }
};
