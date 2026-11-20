/**
 * build:web 预览用 fs mock（真机走 libjsapi_fs.so / readex-nt 同款）。
 * 形状对齐 native/fs_plugin.c 的 API 面。
 */
const PRESET_FILES = ['示例文档.pdf', '照片01.jpg', '笔记.txt'];

export default {
  async readdir(path, opts) {
    if (opts && opts.withFileTypes) {
      return PRESET_FILES.map((name) => ({
        name,
        path: path + '/' + name,
        isFile: () => true,
        isDirectory: () => false
      }));
    }
    return PRESET_FILES.slice();
  },
  async stat(path) {
    return { size: 1024, mode: 0, mtimeMs: Date.now(), isFile: true, isDirectory: false };
  },
  async exists() {
    return true;
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
