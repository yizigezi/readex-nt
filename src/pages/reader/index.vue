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
            this.pageText = '正在读取：' + path
            this.pageTexts = []
            this.currentPage = 0
            try {
                const Readex = await loadReadex()
                if (!Readex || typeof Readex.openBook !== 'function') {
                    throw new Error('Readex 原生模块未加载，请确认 libjsapi_readex.so 已打包安装')
                }
                const book = Readex.openBook(path)
                const info = book.getInfo()
                const chapters = book.getChapters()
                if (!chapters || chapters.length === 0) {
                    throw new Error('文件中没有可读取的章节')
                }
                const chapter = book.getChapter(0)
                const text = chapter && chapter.text ? chapter.text : ''
                if (!text) throw new Error('书籍内容为空')

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
                if (this.book && typeof this.book.close === 'function') {
                    try { this.book.close() } catch (e) {}
                }
                this.book = book
                this.bookTitle = info && info.title ? info.title : path
                this.pageTexts = pageTexts
                this.currentPage = 0
                this.updatePage()
            } catch (e) {
                this.pageText = '打开失败：' + (e && e.message ? e.message : String(e))
                this.pageNumber = '—'
                this.status = 'error'
            }
        },
        updatePage() {
            if (!this.pageTexts.length) return
            this.pageText = this.pageTexts[this.currentPage] || ''
            this.pageNumber = (this.currentPage + 1) + '/' + this.pageTexts.length
        },
        previousPage() {
            if (this.currentPage <= 0) return
            this.currentPage--
            this.updatePage()
        },
        nextPage() {
            if (this.currentPage >= this.pageTexts.length - 1) return
            this.currentPage++
            this.updatePage()
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
