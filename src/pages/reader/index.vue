<template>
    <div class="reader-page">
        <div class="left-rail">
            <div class="left-rail-top" @click="goBack">
                <text class="rail-back">‹</text>
            </div>
            <div class="right-action" @click="previousPage">
                <text class="right-icon">‹</text>
                <text class="right-label">上一页</text>
            </div>
        </div>

        <div class="center-content reader-center">
            <text class="reader-text">{{pageText}}</text>
        </div>

        <div class="right-rail">
            <div class="right-action" @click="nextPage">
                <text class="right-icon">›</text>
                <text class="right-label">下一页</text>
            </div>
            <div class="right-action">
                <text class="right-icon">{{pageNumber}}</text>
                <text class="right-label">页码</text>
            </div>
        </div>
    </div>
</template>

<script>
import { getCurrentBookPath } from '../../utils/BookSession.js'

const CONTENT_WIDTH = 620
const CONTENT_HEIGHT = 220

function jsIndexForByte(str, byteOff) {
    let bytes = 0
    let i = 0
    while (i < str.length && bytes < byteOff) {
        const c = str.charCodeAt(i)
        if (c < 0x80) bytes += 1
        else if (c < 0x800) bytes += 2
        else if (c >= 0xD800 && c < 0xDC00 && i + 1 < str.length) {
            bytes += 4
            i++
        } else bytes += 3
        i++
    }
    return i
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
            bookTitle: '',
            status: ''
        }
    },
    onShow() {
        this.loadBook()
    },
    methods: {
        async loadBook() {
            const path = getCurrentBookPath()
            if (!path) {
                this.pageText = '没有选择书籍，请返回本地书架重新打开。'
                return
            }
            this.pageText = '正在读取本地文件…'
            this.pageTexts = []
            this.currentPage = 0
            let openedBook = null
            try {
                const Readex = await loadReadex()
                if (!Readex || typeof Readex.openBook !== 'function') {
                    throw new Error('Readex 原生模块未加载，请确认 libjsapi_readex.so 已打包安装')
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
                this.bookTitle = info && info.title ? info.title : path
                this.chapters = chapters
                this.currentChapter = 0
                await this.loadChapter(0, 0)
            } catch (e) {
                if (openedBook && typeof openedBook.close === 'function') {
                    try { openedBook.close() } catch (closeError) {}
                }
                this.pageText = '打开失败：' + (e && e.message ? e.message : String(e))
                this.pageNumber = '—'
                this.status = 'error'
            }
        },
        async loadChapter(chapterIndex, pageIndex) {
            if (!this.book || chapterIndex < 0 || chapterIndex >= this.chapters.length) return
            const chapter = this.book.getChapter(chapterIndex)
            const text = chapter && chapter.text ? chapter.text : ''
            if (!text) throw new Error('章节内容为空：' + (chapter && chapter.title ? chapter.title : chapterIndex))
            const Readex = await loadReadex()
            const pages = Readex.paginate(text, {
                width: CONTENT_WIDTH,
                height: CONTENT_HEIGHT,
                fontSize: 17,
                lineHeight: 1.55,
                paragraphSpacing: 8,
                paddingTop: 0,
                paddingBottom: 0,
                paddingLeft: 0,
                paddingRight: 0
            })
            const pageTexts = []
            if (pages && pages.length) {
                for (let i = 0; i < pages.length; i++) {
                    pageTexts.push(text.substring(
                        jsIndexForByte(text, pages[i].startOffset),
                        jsIndexForByte(text, pages[i].endOffset)
                    ))
                }
            }
            if (pageTexts.length === 0) pageTexts.push(text)
            this.currentChapter = chapterIndex
            this.pageTexts = pageTexts
            this.currentPage = Math.max(0, Math.min(pageIndex || 0, pageTexts.length - 1))
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
