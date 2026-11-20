/**
 * build:web 预览用 fs mock（真机走 libjsapi_fs.so / readex-nt 同款）。
 * 形状对齐 native/fs_plugin.c 的 API 面。
 * 预置一个贴近真实的书架场景：文件夹 + 几本 TXT。
 */
const TREE = {
  '/userdisk/Favorite': [
    { name: '书籍', isDir: true },
    { name: '笔记', isDir: true },
    { name: '三体.txt', isDir: false },
    { name: '红楼梦.txt', isDir: false }
  ],
  '/userdisk/Favorite/书籍': [
    { name: '活着.txt', isDir: false },
    { name: '平凡的世界.txt', isDir: false },
    { name: '百年孤独.txt', isDir: false }
  ],
  '/userdisk/Favorite/笔记': [
    { name: '英语笔记.txt', isDir: false }
  ]
};

function list(path) {
  return TREE[path] || [];
}

export default {
  async readdir(path, opts) {
    const items = list(path);
    if (opts && opts.withFileTypes) {
      return items.map((it) => ({
        name: it.name,
        path: path + '/' + it.name,
        isFile: () => !it.isDir,
        isDirectory: () => it.isDir
      }));
    }
    return items.map((it) => it.name);
  },
  async stat(path) {
    const dir = Object.prototype.hasOwnProperty.call(TREE, path);
    return { size: 1024, mode: 0, mtimeMs: Date.now(), isFile: !dir, isDirectory: dir };
  },
  async exists(path) {
    return Object.prototype.hasOwnProperty.call(TREE, path) || list(path.slice(0, path.lastIndexOf('/'))).some((it) => path.endsWith(it.name));
  },
  async mkdir() {
    return true;
  },
  async rm() {
    return true;
  },
  async unlink() {
    return true;
  },
  async readFile() {
    return '';
  },
  async writeFile() {}
};
