import { fs } from './FileSystem/FileSystem.js'

export const LOG_PATH = '/userdisk/readex/app.ui.log'
const LOG_DIR = '/userdisk/readex'
const MAX_LOG_CHARS = 32 * 1024

let writeQueue = Promise.resolve()

function formatError(error) {
    if (!error) return 'Unknown error'
    const message = error && error.message ? error.message : String(error)
    const stack = error && error.stack ? String(error.stack) : ''
    return stack ? message + '\n' + stack : message
}

async function appendLine(line) {
    await fs.mkdir(LOG_DIR)

    let previous = ''
    try {
        previous = String(await fs.readFile(LOG_PATH) || '')
    } catch (error) {
        // A missing log file is expected on first launch.
    }

    const next = (previous + line + '\n').slice(-MAX_LOG_CHARS)
    await fs.writeFile(LOG_PATH, next)
}

/** 等待此前排队的 UI 日志真正写入磁盘。用于调用同步且可能长时间阻塞的原生 API 前。 */
export async function flushLogs() {
    await writeQueue
}

/**
 * 日志写入在后台串行执行，但调用方不应等待文件系统完成。
 * 这样设备上的慢速 fs / execShell 不会阻塞阅读器加载和页面渲染。
 */
export function logEvent(stage, detail) {
    let suffix = ''
    if (detail !== undefined && detail !== null) {
        try {
            suffix = ' ' + (typeof detail === 'string' ? detail : JSON.stringify(detail))
        } catch (error) {
            suffix = ' [detail serialization failed: ' + String(error) + ']'
        }
    }
    const line = '[' + new Date().toISOString() + '] ' + String(stage) + suffix

    writeQueue = writeQueue.then(() => appendLine(line)).catch(error => {
        try {
            console.error('[Readex logger] write failed:', formatError(error))
        } catch (ignored) {}
    })
    return Promise.resolve()
}

export function logError(stage, error) {
    return logEvent(stage + ' ERROR', formatError(error))
}

logEvent('logger.initialized', 'path=' + LOG_PATH)
