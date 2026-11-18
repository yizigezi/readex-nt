import { fs } from './FileSystem/FileSystem.js'

export const LOG_PATH = '/userdisk/readex/app.ui.log'
const LOG_DIR = '/userdisk/readex'
const MAX_LOG_CHARS = 128 * 1024

let writeQueue = Promise.resolve()

function formatError(error) {
    if (!error) return 'Unknown error'
    const message = error && error.message ? error.message : String(error)
    const stack = error && error.stack ? String(error.stack) : ''
    return stack ? message + '\\n' + stack : message
}

async function appendLine(line) {
    await fs.mkdir(LOG_DIR)

    // The shell fallback returns a boolean from mkdir; verify the directory
    // instead of silently treating a failed mkdir as a successful write.
    if (!await fs.exists(LOG_DIR)) {
        throw new Error('无法创建日志目录：' + LOG_DIR)
    }

    let previous = ''
    if (await fs.exists(LOG_PATH)) {
        previous = String(await fs.readFile(LOG_PATH) || '')
    }

    const next = (previous + line + '\\n').slice(-MAX_LOG_CHARS)
    await fs.writeFile(LOG_PATH, next)

    if (!await fs.exists(LOG_PATH)) {
        throw new Error('日志文件写入后仍不存在：' + LOG_PATH)
    }
}

/**
 * 使用项目自己的 FileSystem 适配层写入诊断日志。
 * 所有写入串行执行，避免多个页面事件互相覆盖。
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

    return writeQueue
}

export function logError(stage, error) {
    return logEvent(stage + ' ERROR', formatError(error))
}

// 模块加载时立即创建首条日志，不依赖用户先打开书籍。
logEvent('logger.initialized', 'path=' + LOG_PATH)
