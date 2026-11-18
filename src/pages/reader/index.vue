<template>
    <div class="reader-page">
        <div class="left-rail">
            <div class="rail-button back-button" @click="goBack">
                <image class="material-icon" src="/assets/icons/arrow_back.png" />
            </div>
        </div>

        <div class="center-content reader-center">
            <text class="reader-text" :style="readerTextStyle">{{pageText}}</text>
        </div>

        <div class="right-rail reader-rail">
            <div class="rail-button" @click="previousPage">
                <image class="material-icon" src="/assets/icons/chevron_left.png" />
            </div>
            <div class="rail-button" @click="nextPage">
                <image class="material-icon" src="/assets/icons/chevron_right.png" />
            </div>
            <div class="rail-button" @click="toggleSettings">
                <image class="material-icon" src="/assets/icons/settings.png" />
            </div>
            <div class="rail-button" @click="toggleNativeDebug">
                <image class="material-icon" src="/assets/icons/description.png" />
            </div>
        </div>

        <div class="native-debug-mask" v-if="nativeDebugVisible">
            <div class="native-debug-panel">
                <div class="native-debug-header">
                    <div class="native-debug-heading">
                        <text class="native-debug-title">原生模块诊断</text>
                        <text class="native-debug-status">{{nativeModuleStatus}}</text>
                    </div>
                    <div class="native-debug-actions">
                        <div class="debug-action" @click="refreshNativeDebug">
                            <image class="material-icon" src="/assets/icons/refresh.png" />
                        </div>
                        <div class="debug-action" @click="toggleNativeDebug">
                            <image class="material-icon" src="/assets/icons/close.png" />
                        </div>
                    </div>
                </div>
                <div class="native-debug-info">
                    <div class="native-debug-item">
                        <text class="native-debug-label">模块版本</text>
                        <text class="native-debug-value">{{nativeInfo.version || '—'}}</text>
                    </div>
                    <div class="native-debug-item">
                        <text class="native-debug-label">QuickJS</text>
                        <text class="native-debug-value">{{nativeInfo.quickjs || '—'}}</text>
                    </div>
                    <div class="native-debug-item">
                        <text class="native-debug-label">平台 / 构建</text>
                        <text class="native-debug-value">{{nativeInfo.platform || '—'}} / {{nativeInfo.buildMode || '—'}}</text>
                    </div>
                    <div class="native-debug-item">
                        <text class="native-debug-label">API</text>
                        <text class="native-debug-value">{{nativeApiSummary}}</text>
                    </div>
                    <div class="native-debug-item">
                        <text class="native-debug-label">当前 Book</text>
                        <text class="native-debug-value">{{book ? '已打开' : '未打开'}} · {{chapters.length}} 章</text>
                    </div>
                </div>
                <text class="native-debug-log-title">原生日志 · app.native.log</text>
                <scroller class="native-debug-log-scroll" scroll-direction="vertical" show-scrollbar="false">
                    <text class="native-debug-log-line" v-for="(line, index) in nativeLogLines" :key="index">{{line}}</text>
                </scroller>
                <text class="native-debug-error" v-if="nativeDebugError">{{nativeDebugError}}</text>
            </div>
        </div>

        <div class="settings-mask" v-if="settingsVisible" @click="closeSettings">
            <div class="settings-panel" @click.stop>
                <div class="settings-header">
                    <text class="settings-title">阅读设置</text>
                    <div class="settings-close" @click="closeSettings"><image class="material-icon" src="/assets/icons/close.png" /></div>
                </div>
                <div class="setting-row">
                    <text class="setting-label">字号</text>
                    <div class="setting-stepper">
                        <div class="setting-control" @click="changeFontSize(-1)"><image class="material-icon" src="/assets/icons/remove.png" /></div>
                        <text class="setting-value">{{settings.fontSize}} px</text>
                        <div class="setting-control" @click="changeFontSize(1)"><image class="material-icon" src="/assets/icons/add.png" /></div>
                    </div>
                </div>
                <div class="setting-row">
                    <text class="setting-label">行距</text>
                    <div class="setting-stepper">
                        <div class="setting-control" @click="changeLineHeight(-0.05)"><image class="material-icon" src="/assets/icons/remove.png" /></div>
                        <text class="setting-value">{{settings.lineHeight.toFixed(2)}} ×</text>
                        <div class="setting-control" @click="changeLineHeight(0.05)"><image class="material-icon" src="/assets/icons/add.png" /></div>
                    </div>
                </div>
            </div>
        </div>
    </div>
</template>

<script>
import { getCurrentBookPath } from '../../utils/BookSession.js'
import { fs } from '../../utils/FileSystem/FileSystem.js'
import { logEvent, logError } from '../../utils/Logger.js'
import {
    loadSettings,
    saveSettings,
    loadBookCatalog,
    displayBookTitle
} from '../../utils/BookLibrary.js'

const SCREEN_WIDTH = 800
const SCREEN_HEIGHT = 254
const LEFT_RAIL_WIDTH = 68
const RIGHT_RAIL_WIDTH = 76
const CONTENT_WIDTH = SCREEN_WIDTH - LEFT_RAIL_WIDTH - RIGHT_RAIL_WIDTH
const PADDING_LEFT = 16
const PADDING_RIGHT = 16
const PADDING_TOP = 12
const PADDING_BOTTOM = 12

function byteOffsetToStringIndex(value, byteOffset) {
    const target = Math.max(0, Number(byteOffset) || 0)
    let bytes = 0
    let index = 0
    while (index < value.length && bytes < target) {
        const cp = value.codePointAt(index)
        let byteLength = 1
        if (cp > 0x7F && cp <= 0x7FF) byteLength = 2
        else if (cp > 0x7FF && cp <= 0xFFFF) byteLength = 3
        else if (cp > 0xFFFF) byteLength = 4
        bytes += byteLength
        index += cp > 0xFFFF ? 2 : 1
    }
    return index
}

async function loadReadex() {
    const mod = await import('readex')
    return mod && (mod.default || mod)
}

export default {
    name: 'reader',
    data() {
        return {
            pageText: '正在打开本地书籍…',
            pageNumber: '—',
            pageTexts: [],
            currentPage: 0,
            currentChapter: 0,
            chapters: [],
            book: null,
            bookPath: '',
            bookTitle: '',
            settings: { fontSize: 17, lineHeight: 1.55 },
            settingsVisible: false,
            nativeDebugVisible: false,
            nativeModuleStatus: '尚未检测',
            nativeInfo: {},
            nativeApiSummary: '—',
            nativeLogLines: ['等待读取原生日志…'],
            nativeDebugError: '',
            nativeDebugRefreshing: false,
            loading: false
        }
    },
    computed: {
        readerTextStyle() {
            return {
                fontSize: this.settings.fontSize + 'px',
                lineHeight: (this.settings.fontSize * this.settings.lineHeight) + 'px'
            }
        }
    },
    onShow() {
        this.loadBook()
    },
    methods: {
        paginationConfig() {
            return {
                width: CONTENT_WIDTH,
                height: SCREEN_HEIGHT,
                fontSize: this.settings.fontSize,
                lineHeight: this.settings.lineHeight,
                paragraphSpacing: this.settings.fontSize * 0.45,
                paddingTop: PADDING_TOP,
                paddingBottom: PADDING_BOTTOM,
                paddingLeft: PADDING_LEFT,
                paddingRight: PADDING_RIGHT
            }
        },
        async loadBook() {
            const path = getCurrentBookPath()
            await logEvent('reader.loadBook.start', 'path=' + (path || '<empty>'))
            if (!path) {
                this.pageText = '没有选择书籍，请返回首页。'
                await logEvent('reader.loadBook.abort', 'no book path')
                return
            }
            this.loading = true
            this.bookPath = path
            this.pageText = '正在读取本地文件…'
            this.pageTexts = []
            this.currentPage = 0
            let openedBook = null
            try {
                await logEvent('reader.loadBook.loadSettings.begin')
                this.settings = await loadSettings()
                await logEvent('reader.loadBook.loadSettings.done', this.settings)

                await logEvent('reader.loadBook.loadCatalog.begin')
                const catalog = await loadBookCatalog()
                this.bookTitle = displayBookTitle(catalog, path)
                await logEvent('reader.loadBook.loadCatalog.done', 'title=' + this.bookTitle)

                await logEvent('reader.loadBook.importReadex.begin')
                const Readex = await loadReadex()
                await logEvent('reader.loadBook.importReadex.done', 'available=' + !!Readex)
                if (!Readex || typeof Readex.openBook !== 'function') {
                    throw new Error('Readex 原生模块未加载，请确认 libjsapi_readex.so 已安装')
                }

                // openBook is synchronous and may scan the entire file. The
                // "begin" entry is flushed before entering native code so a
                // hang can be distinguished from later chapter/pagination work.
                await logEvent('reader.loadBook.openBook.begin', 'path=' + path)
                openedBook = Readex.openBook(path)
                await logEvent('reader.loadBook.openBook.done')

                await logEvent('reader.loadBook.getInfo.begin')
                const info = openedBook.getInfo()
                await logEvent('reader.loadBook.getInfo.done', info && info.title)

                await logEvent('reader.loadBook.getChapters.begin')
                const chapters = openedBook.getChapters()
                await logEvent('reader.loadBook.getChapters.done', 'count=' + (chapters ? chapters.length : 0))
                if (!chapters || chapters.length === 0) {
                    throw new Error('文件中没有可读取的章节')
                }
                if (this.book && typeof this.book.close === 'function') {
                    try { this.book.close() } catch (e) {}
                }
                this.book = openedBook
                openedBook = null
                this.bookTitle = this.bookTitle || (info && info.title) || path
                this.chapters = chapters
                this.currentChapter = 0
                await logEvent('reader.loadBook.loadChapter.begin', 'index=0')
                await this.loadChapter(0, 0)
                await logEvent('reader.loadBook.done', 'chapters=' + chapters.length)
            } catch (e) {
                await logError('reader.loadBook', e)
                if (openedBook && typeof openedBook.close === 'function') {
                    try { openedBook.close() } catch (closeError) {
                        await logError('reader.loadBook.close', closeError)
                    }
                }
                this.pageText = '打开失败：' + (e && e.message ? e.message : String(e))
                this.pageNumber = '—'
            } finally {
                this.loading = false
                await logEvent('reader.loadBook.finally', 'loading=false')
            }
        },
        async loadChapter(chapterIndex, pageIndex) {
            if (!this.book || chapterIndex < 0 || chapterIndex >= this.chapters.length) return
            await logEvent('reader.loadChapter.getChapter.begin', 'index=' + chapterIndex)
            const chapter = this.book.getChapter(chapterIndex)
            const text = chapter && chapter.text ? chapter.text : ''
            await logEvent('reader.loadChapter.getChapter.done', 'textLength=' + text.length)
            if (!text) throw new Error('章节内容为空')
            await logEvent('reader.loadChapter.paginate.begin', 'index=' + chapterIndex)
            const pages = this.book.paginateChapter(chapterIndex, this.paginationConfig())
            await logEvent('reader.loadChapter.paginate.done', 'pageCount=' + (pages ? pages.length : 0))
            const pageTexts = []
            if (pages && pages.length) {
                for (let i = 0; i < pages.length; i++) {
                    const start = byteOffsetToStringIndex(text, pages[i].startOffset)
                    const end = byteOffsetToStringIndex(text, pages[i].endOffset)
                    pageTexts.push(text.substring(start, end))
                }
            }
            if (pageTexts.length === 0) pageTexts.push(text)
            this.currentChapter = chapterIndex
            this.pageTexts = pageTexts
            this.currentPage = Math.max(0, Math.min(Number(pageIndex) || 0, pageTexts.length - 1))
            this.updatePage()
        },
        updatePage() {
            if (!this.pageTexts.length) return
            this.pageText = this.pageTexts[this.currentPage] || ''
            this.pageNumber = (this.currentPage + 1) + '/' + this.pageTexts.length
        },
        async previousPage() {
            if (this.currentPage > 0) {
                this.currentPage--
                this.updatePage()
                return
            }
            if (this.currentChapter > 0) {
                try {
                    await this.loadChapter(this.currentChapter - 1, 0)
                    this.currentPage = this.pageTexts.length - 1
                    this.updatePage()
                } catch (e) {
                    this.pageText = '上一章加载失败：' + (e && e.message ? e.message : String(e))
                }
            }
        },
        async nextPage() {
            if (this.currentPage < this.pageTexts.length - 1) {
                this.currentPage++
                this.updatePage()
                return
            }
            if (this.currentChapter < this.chapters.length - 1) {
                try {
                    await this.loadChapter(this.currentChapter + 1, 0)
                } catch (e) {
                    this.pageText = '下一章加载失败：' + (e && e.message ? e.message : String(e))
                }
            }
        },
        toggleSettings() {
            this.settingsVisible = !this.settingsVisible
        },
        async toggleNativeDebug() {
            this.nativeDebugVisible = !this.nativeDebugVisible
            if (this.nativeDebugVisible) await this.refreshNativeDebug()
        },
        async refreshNativeDebug() {
            if (this.nativeDebugRefreshing) return
            this.nativeDebugRefreshing = true
            this.nativeDebugError = ''
            this.nativeModuleStatus = '正在检测…'
            try {
                const Readex = await loadReadex()
                if (!Readex) throw new Error('readex 模块导入结果为空')
                this.nativeModuleStatus = '模块已加载'
                this.nativeInfo = typeof Readex.info === 'function' ? (Readex.info() || {}) : {}
                this.nativeApiSummary = [
                    'openBook:' + (typeof Readex.openBook === 'function' ? 'OK' : '缺失'),
                    'paginate:' + (typeof Readex.paginate === 'function' ? 'OK' : '缺失'),
                    'closeAllBooks:' + (typeof Readex.closeAllBooks === 'function' ? 'OK' : '缺失')
                ].join('  ')
            } catch (error) {
                this.nativeModuleStatus = '模块不可用'
                this.nativeInfo = {}
                this.nativeApiSummary = '不可读取'
                this.nativeDebugError = '模块检测失败：' + (error && error.message ? error.message : String(error))
                await logError('reader.nativeDebug.module', error)
            }
            try {
                const path = '/userdisk/readex/app.native.log'
                if (await fs.exists(path)) {
                    const content = String(await fs.readFile(path) || '')
                    const lines = content.split(/\r?\n/).filter(Boolean)
                    this.nativeLogLines = lines.slice(-24)
                    if (!this.nativeLogLines.length) this.nativeLogLines = ['原生日志文件为空']
                } else {
                    this.nativeLogLines = ['尚未生成 app.native.log', '日志路径：' + path]
                }
            } catch (error) {
                this.nativeLogLines = ['读取原生日志失败']
                this.nativeDebugError = (this.nativeDebugError ? this.nativeDebugError + '；' : '') +
                    (error && error.message ? error.message : String(error))
                await logError('reader.nativeDebug.log', error)
            } finally {
                this.nativeDebugRefreshing = false
            }
        },
        closeSettings() {
            this.settingsVisible = false
        },
        async changeFontSize(delta) {
            const value = Math.max(12, Math.min(30, this.settings.fontSize + delta))
            if (value === this.settings.fontSize) return
            this.settings.fontSize = value
            await this.applySettings()
        },
        async changeLineHeight(delta) {
            const value = Math.max(1.1, Math.min(2.2, Math.round((this.settings.lineHeight + delta) * 100) / 100))
            if (value === this.settings.lineHeight) return
            this.settings.lineHeight = value
            await this.applySettings()
        },
        async applySettings() {
            try {
                this.settings = await saveSettings(this.settings)
                if (this.book) await this.loadChapter(this.currentChapter, this.currentPage)
            } catch (e) {
                this.pageText = '保存设置失败：' + (e && e.message ? e.message : String(e))
            }
        },
        goBack() {
            if (this.book && typeof this.book.close === 'function') {
                try { this.book.close() } catch (e) {}
                this.book = null
            }
            this.$page.finish()
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';

.native-debug-mask {
    position: absolute;
    left: 76px;
    right: 84px;
    top: 8px;
    bottom: 8px;
    z-index: 30;
}

.native-debug-panel {
    width: 100%;
    height: 100%;
    box-sizing: border-box;
    padding: 10px 14px;
    background-color: #17191d;
    border: 1px solid #41454d;
    display: flex;
    flex-direction: column;
}

.native-debug-header,
.native-debug-actions,
.native-debug-info,
.native-debug-item {
    display: flex;
    flex-direction: row;
    align-items: center;
}

.native-debug-header {
    justify-content: space-between;
    min-height: 28px;
}

.native-debug-heading {
    display: flex;
    flex-direction: row;
    align-items: baseline;
}

.native-debug-title {
    color: #f2f3f5;
    font-size: 16px;
    font-weight: 600;
}

.native-debug-status {
    color: #a9c7ff;
    font-size: 11px;
    margin-left: 12px;
}

.native-debug-actions {
    gap: 12px;
}

.debug-action {
    width: 30px;
    height: 28px;
    display: flex;
    align-items: center;
    justify-content: center;
}

.native-debug-info {
    flex-wrap: wrap;
    margin-top: 6px;
    padding-bottom: 5px;
    border-bottom: 1px solid #383c43;
}

.native-debug-item {
    width: 50%;
    min-height: 22px;
}

.native-debug-label {
    color: #9299a5;
    font-size: 11px;
    width: 76px;
}

.native-debug-value {
    color: #e0e3e8;
    font-size: 11px;
    flex: 1;
}

.native-debug-log-title {
    color: #c4cad4;
    font-size: 11px;
    margin-top: 5px;
    margin-bottom: 3px;
}

.native-debug-log-scroll {
    flex: 1;
    min-height: 0;
}

.native-debug-log-line {
    color: #c4cbd5;
    font-size: 10px;
    line-height: 13px;
}

.native-debug-error {
    color: #ffb4ab;
    font-size: 10px;
    margin-top: 3px;
}
</style>
