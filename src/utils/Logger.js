import { getFs } from './FileSystem/FileSystem.js'

export const LOG_PATH = '/userdisk/readex/app.log'
const MAX_LOG_CHARS = 128 * 1024

let writeQueue = Promise.resolve()

function formatError(error) {
    if (!error) return 'Unknown error'
    const message = error && error.message ? error.message : String(error)
    const stack = error && error.stack ? String(error.stack) : ''
    return stack ? message + '\n' + stack : message
}

async function appendLine(line) {
    const fs = await getFs()
    await fs.mkdir('/userdisk/readex')

    let previous = ''
    try {
        if (await fs.exists(LOG_PATH)) {
            previous = String(await fs.readFile(LOG_PATH) || '')
        }
    } catch (error) {
        // A failed read should not prevent creating a fresh diagnostic log.
    }

    const next = (previous + line + '\n').slice(-MAX_LOG_CHARS)
    await fs.writeFile(LOG_PATH, next)
}

/**
 * Append a timestamped diagnostic entry to /userdisk/readex/app.log.
 * Writes are serialized so concurrent page events do not overwrite each other.
 */
export function logEvent(stage, detail) {
    const suffix = detail === undefined || detail === null
        ? ''
        : ' ' + (typeof detail === 'string' ? detail : JSON.stringify(detail))
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
