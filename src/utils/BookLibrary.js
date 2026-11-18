import { getFs } from './FileSystem/FileSystem.js'

export const READEX_HOME = '/userdisk/readex'
export const SETTINGS_PATH = READEX_HOME + '/settings.json'
export const BOOKS_PATH = READEX_HOME + '/books.json'
export const DEFAULT_BOOK_DIR = '/userdisk/Favorite'

const DEFAULT_SETTINGS = {
    fontSize: 17,
    lineHeight: 1.55
}

async function readJson(path, fallback) {
    const fs = await getFs()
    try {
        if (!(await fs.exists(path))) return fallback
        const raw = await fs.readFile(path)
        const value = JSON.parse(String(raw || ''))
        return value && typeof value === 'object' ? value : fallback
    } catch (error) {
        return fallback
    }
}

async function writeJson(path, value) {
    const fs = await getFs()
    await fs.mkdir(READEX_HOME)
    await fs.writeFile(path, JSON.stringify(value, null, 2))
}

export async function loadSettings() {
    const saved = await readJson(SETTINGS_PATH, {})
    return {
        fontSize: clampNumber(saved.fontSize, 12, 30, DEFAULT_SETTINGS.fontSize),
        lineHeight: clampNumber(saved.lineHeight, 1.1, 2.2, DEFAULT_SETTINGS.lineHeight)
    }
}

export async function saveSettings(settings) {
    const normalized = {
        fontSize: clampNumber(settings.fontSize, 12, 30, DEFAULT_SETTINGS.fontSize),
        lineHeight: clampNumber(settings.lineHeight, 1.1, 2.2, DEFAULT_SETTINGS.lineHeight)
    }
    await writeJson(SETTINGS_PATH, normalized)
    return normalized
}

export async function loadBookCatalog() {
    const saved = await readJson(BOOKS_PATH, {})
    const books = saved.books && typeof saved.books === 'object' ? saved.books : {}
    const recent = Array.isArray(saved.recent) ? saved.recent : []
    return { books, recent }
}

export async function getBookProgress(path) {
    if (!path) return null
    const catalog = await loadBookCatalog()
    const book = catalog.books[path]
    const progress = book && book.progress
    if (!progress || typeof progress !== 'object') return null
    return {
        chapterIndex: Math.max(0, Math.floor(Number(progress.chapterIndex) || 0)),
        pageIndex: Math.max(0, Math.floor(Number(progress.pageIndex) || 0))
    }
}

export async function saveBookProgress(path, progress) {
    if (!path || !progress || typeof progress !== 'object') return null
    const catalog = await loadBookCatalog()
    const old = catalog.books[path] || {}
    const saved = {
        chapterIndex: Math.max(0, Math.floor(Number(progress.chapterIndex) || 0)),
        pageIndex: Math.max(0, Math.floor(Number(progress.pageIndex) || 0)),
        updatedAt: Date.now()
    }
    catalog.books[path] = Object.assign({}, old, { progress: saved })
    await writeJson(BOOKS_PATH, catalog)
    return saved
}

export async function recordBookOpen(path, fallbackTitle) {
    const catalog = await loadBookCatalog()
    const old = catalog.books[path] || {}
    catalog.books[path] = {
        title: old.title || fallbackTitle || fileName(path),
        lastOpened: Date.now()
    }
    catalog.recent = [path].concat(catalog.recent.filter(item => item !== path)).slice(0, 20)
    await writeJson(BOOKS_PATH, catalog)
    return catalog
}

export async function setBookTitle(path, title) {
    const catalog = await loadBookCatalog()
    const old = catalog.books[path] || {}
    catalog.books[path] = Object.assign({}, old, {
        title: String(title || '').trim() || fileName(path)
    })
    await writeJson(BOOKS_PATH, catalog)
    return catalog
}

export function displayBookTitle(catalog, path) {
    const item = catalog && catalog.books ? catalog.books[path] : null
    return item && item.title ? item.title : fileName(path)
}

export function fileName(path) {
    const parts = String(path || '').split('/')
    return parts[parts.length - 1] || path
}

function clampNumber(value, min, max, fallback) {
    const n = Number(value)
    if (!isFinite(n)) return fallback
    return Math.max(min, Math.min(max, n))
}
