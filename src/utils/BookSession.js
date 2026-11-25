// 当前待阅读文件的轻量会话。页面间只传路径，Book 句柄由阅读页自行管理。
let currentBookPath = '';

export function setCurrentBookPath(path) {
    currentBookPath = typeof path === 'string' ? path : '';
}

export function getCurrentBookPath() {
    return currentBookPath;
}
