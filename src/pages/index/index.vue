<template>
    <div class="home-page">
        <div class="left-rail">
            <div class="rail-button back-button" @click="goBack">
                <text class="rail-icon">‹</text>
            </div>
        </div>

        <div class="center-content home-center">
            <div class="home-heading">
                <text class="home-title">{{browseMode ? '本地文件' : '最近阅读'}}</text>
                <text class="home-path" v-if="browseMode">{{currentPath}}</text>
            </div>

            <div class="library-status" v-if="loading">
                <text class="status-text">正在读取…</text>
            </div>
            <div class="library-status" v-else-if="error">
                <text class="status-text">{{error}}</text>
            </div>
            <div class="library-status" v-else-if="!browseMode && recentBooks.length === 0">
                <text class="status-text">还没有最近阅读的书籍</text>
            </div>
            <div class="library-status" v-else-if="browseMode && entries.length === 0">
                <text class="status-text">这个文件夹里没有 TXT 文件</text>
            </div>

            <scroll-view v-else class="shelf-row" scroll-x="true" scroll-y="false">
                <div
                    class="file-card"
                    v-for="entry in visibleBooks"
                    :key="entry.path"
                    @click="openEntry(entry)"
                >
                    <div class="card-top">
                        <text class="file-kind">{{entry.isDirectory ? '▰' : '▤'}}</text>
                        <text class="rename-icon" v-if="!entry.isDirectory" @click.stop="startRename(entry)">✎</text>
                    </div>
                    <text class="file-title">{{entry.title || entry.name}}</text>
                    <text class="file-meta" v-if="entry.isDirectory">文件夹</text>
                </div>
            </scroll-view>
        </div>

        <div class="right-rail">
            <div class="rail-button" @click="toggleBrowse">
                <text class="rail-icon">{{browseMode ? '◷' : '▱'}}</text>
            </div>
            <div class="rail-button" @click="refresh">
                <text class="rail-icon">⟳</text>
            </div>
            <div class="rail-button" v-if="browseMode" @click="goParent">
                <text class="rail-icon">↑</text>
            </div>
        </div>

        <div class="dialog-mask" v-if="renameVisible">
            <div class="rename-dialog">
                <text class="dialog-title">设置书名</text>
                <input class="rename-input" v-model="renameValue" placeholder="输入书名" />
                <div class="dialog-actions">
                    <div class="dialog-button" @click="cancelRename"><text class="dialog-button-icon">×</text></div>
                    <div class="dialog-button" @click="saveRename"><text class="dialog-button-icon">✓</text></div>
                </div>
            </div>
        </div>
    </div>
</template>

<script>
import { getFs } from '../../utils/FileSystem/FileSystem.js'
import { setCurrentBookPath } from '../../utils/BookSession.js'
import {
    DEFAULT_BOOK_DIR,
    loadBookCatalog,
    recordBookOpen,
    setBookTitle,
    displayBookTitle,
    fileName
} from '../../utils/BookLibrary.js'

function isTextFile(name) {
    return /\.txt$/i.test(String(name || ''))
}

function isDirectory(item) {
    return typeof item.isDirectory === 'function' ? item.isDirectory() : !!item.isDirectory
}

export default {
    name: 'index',
    data() {
        return {
            browseMode: false,
            currentPath: DEFAULT_BOOK_DIR,
            entries: [],
            recentBooks: [],
            catalog: { books: {}, recent: [] },
            loading: false,
            error: '',
            renameVisible: false,
            renamePath: '',
            renameValue: ''
        }
    },
    computed: {
        visibleBooks() {
            return this.browseMode ? this.entries : this.recentBooks
        }
    },
    onShow() {
        this.refresh()
    },
    methods: {
        async refresh() {
            this.loading = true
            this.error = ''
            try {
                this.catalog = await loadBookCatalog()
                if (this.browseMode) {
                    await this.loadDirectory()
                } else {
                    await this.loadRecent()
                }
            } catch (e) {
                this.error = e && e.message ? e.message : String(e)
            } finally {
                this.loading = false
            }
        },
        async loadRecent() {
            const fs = await getFs()
            const list = []
            for (let i = 0; i < this.catalog.recent.length; i++) {
                const path = this.catalog.recent[i]
                try {
                    if (!(await fs.exists(path))) continue
                    const stat = await fs.stat(path)
                    if (stat && stat.isDirectory) continue
                    list.push({
                        path,
                        name: fileName(path),
                        title: displayBookTitle(this.catalog, path),
                        isDirectory: false
                    })
                } catch (e) {}
            }
            this.recentBooks = list
        },
        async loadDirectory() {
            const fs = await getFs()
            if (!(await fs.exists(this.currentPath))) {
                throw new Error('默认目录不存在：' + this.currentPath)
            }
            const raw = await fs.readdir(this.currentPath, { withFileTypes: true })
            const result = []
            for (let i = 0; i < raw.length; i++) {
                const item = raw[i]
                const name = item.name || String(item)
                if (!name || name === '.' || name === '..' || name.charAt(0) === '.') continue
                const dir = isDirectory(item)
                if (!dir && !isTextFile(name)) continue
                const path = item.path || this.currentPath.replace(/\/$/, '') + '/' + name
                result.push({
                    name,
                    path,
                    isDirectory: dir,
                    title: dir ? name : displayBookTitle(this.catalog, path)
                })
            }
            result.sort((a, b) => {
                if (a.isDirectory !== b.isDirectory) return a.isDirectory ? -1 : 1
                return a.title.localeCompare(b.title)
            })
            this.entries = result
        },
        async openEntry(entry) {
            if (entry.isDirectory) {
                this.currentPath = entry.path
                this.refresh()
                return
            }
            setCurrentBookPath(entry.path)
            try {
                await recordBookOpen(entry.path, entry.name)
            } catch (e) {}
            $falcon.navTo('reader')
        },
        toggleBrowse() {
            this.browseMode = !this.browseMode
            if (this.browseMode) this.currentPath = DEFAULT_BOOK_DIR
            this.refresh()
        },
        goParent() {
            if (this.currentPath === DEFAULT_BOOK_DIR || this.currentPath === '/') return
            const path = this.currentPath.replace(/\/+$/, '')
            const index = path.lastIndexOf('/')
            const parent = index <= 0 ? '/' : path.slice(0, index)
            if (parent !== '/' && parent.indexOf(DEFAULT_BOOK_DIR) !== 0) return
            this.currentPath = parent
            this.refresh()
        },
        goBack() {
            this.$page.finish()
        },
        startRename(entry) {
            this.renamePath = entry.path
            this.renameValue = entry.title || entry.name
            this.renameVisible = true
        },
        cancelRename() {
            this.renameVisible = false
            this.renamePath = ''
            this.renameValue = ''
        },
        async saveRename() {
            if (!this.renamePath) return
            try {
                this.catalog = await setBookTitle(this.renamePath, this.renameValue)
                await this.refresh()
            } catch (e) {
                this.error = '保存书名失败：' + (e && e.message ? e.message : String(e))
            }
            this.cancelRename()
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';
</style>
