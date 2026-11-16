<template>
    <div class="reader-page">
        <div class="left-rail">
            <div class="rail-button back-button" @click="goBack">
                <text class="rail-icon">‹</text>
            </div>
        </div>

        <div class="center-content reader-center">
            <text class="reader-text" :style="readerTextStyle">{{pageText}}</text>
        </div>

        <div class="right-rail reader-rail">
            <div class="rail-button" @click="previousPage">
                <text class="rail-icon">‹</text>
            </div>
            <div class="rail-button" @click="nextPage">
                <text class="rail-icon">›</text>
            </div>
            <div class="rail-button" @click="toggleSettings">
                <text class="rail-icon">⚙</text>
            </div>
        </div>

        <div class="settings-mask" v-if="settingsVisible" @click="closeSettings">
            <div class="settings-panel" @click.stop>
                <div class="settings-header">
                    <text class="settings-title">阅读设置</text>
                    <div class="settings-close" @click="closeSettings"><text class="settings-control-icon">×</text></div>
                </div>
                <div class="setting-row">
                    <text class="setting-label">字号</text>
                    <div class="setting-stepper">
                        <div class="setting-control" @click="changeFontSize(-1)"><text class="settings-control-icon">−</text></div>
                        <text class="setting-value">{{settings.fontSize}} px</text>
                        <div class="setting-control" @click="changeFontSize(1)"><text class="settings-control-icon">＋</text></div>
                    </div>
                </div>
                <div class="setting-row">
                    <text class="setting-label">行距</text>
                    <div class="setting-stepper">
                        <div class="setting-control" @click="changeLineHeight(-0.05)"><text class="settings-control-icon">−</text></div>
                        <text class="setting-value">{{settings.lineHeight.toFixed(2)}} ×</text>
                        <div class="setting-control" @click="changeLineHeight(0.05)"><text class="settings-control-icon">＋</text></div>
                    </div>
                </div>
            </div>
        </div>
    </div>
</template>

<script>
import { getCurrentBookPath } from '../../utils/BookSession.js'
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
            if (!path) {
                this.pageText = '没有选择书籍，请返回首页。'
                return
            }
            this.loading = true
            this.bookPath = path
            this.pageText = '正在读取本地文件…'
            this.pageTexts = []
            this.currentPage = 0
            let openedBook = null
            try {
                this.settings = await loadSettings()
                const catalog = await loadBookCatalog()
                this.bookTitle = displayBookTitle(catalog, path)
                const Readex = await loadReadex()
                if (!Readex || typeof Readex.openBook !== 'function') {
                    throw new Error('Readex 原生模块未加载，请确认 libjsapi_readex.so 已安装')
                }
                openedBook = Readex.openBook(path)
                const info = openedBook.getInfo()
                const chapters = openedBook.getChapters()
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
                await this.loadChapter(0, 0)
            } catch (e) {
                if (openedBook && typeof openedBook.close === 'function') {
                    try { openedBook.close() } catch (closeError) {}
                }
                this.pageText = '打开失败：' + (e && e.message ? e.message : String(e))
                this.pageNumber = '—'
            } finally {
                this.loading = false
            }
        },
        async loadChapter(chapterIndex, pageIndex) {
            if (!this.book || chapterIndex < 0 || chapterIndex >= this.chapters.length) return
            const chapter = this.book.getChapter(chapterIndex)
            const text = chapter && chapter.text ? chapter.text : ''
            if (!text) throw new Error('章节内容为空')
            const pages = this.book.paginateChapter(chapterIndex, this.paginationConfig())
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
</style>
