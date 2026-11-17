import { execShell } from './FileSystem/FileSystem.js'

export const LOG_PATH = '/userdisk/readex/app.log'
const MAX_LOG_BYTES = 128 * 1024

let writeQueue = Promise.resolve()

function formatError(error) {
    if (!error) return 'Unknown error'
    const message = error && error.message ? error.message : String(error)
    const stack = error && error.stack ? String(error.stack) : ''
    return stack ? message + '\\n' + stack : message
}

function shellQuote(value) {
    return "'" + String(value).replace(/'/g, "'\\''") + "'"
}

async function appendLine(line) {
    const path = shellQuote(LOG_PATH)
    const directory = shellQuote('/userdisk/readex')
    const temporary = shellQuote(LOG_PATH + '.tmp')
    const command = [
        'mkdir -p ' + directory,
        "printf '%s\\n' " + shellQuote(line) + ' >> ' + path,
        'if [ "$(wc -c < ' + path + ')" -gt ' + MAX_LOG_BYTES + ' ]; then',
        '  tail -c ' + MAX_LOG_BYTES + ' ' + path + ' > ' + temporary,
        '  mv ' + temporary + ' ' + path,
        'fi'
    ].join(' && ')

    // Use the already-tested execShell bridge directly. The fs JSAPI differs
    // between firmware builds and may expose readdir without writeFile/mkdir.
    await execShell(command)
}

/**
 * Append a timestamped diagnostic entry to /userdisk/readex/app.log.
 * Writes are serialized so concurrent page events do not overwrite each other.
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

// Create the log file as soon as this module is imported, even before a book
// is selected. This also makes logger initialization failures observable.
logEvent('logger.initialized', 'path=' + LOG_PATH)
