<template>
    <div class="home-page">
        <div class="left-rail">
            <div class="left-rail-top" @click="goBack">
                <text class="rail-back">‹</text>
            </div>
            <div class="rail-caption">
                <text class="rail-label">本地</text>
                <text class="rail-label">书籍</text>
            </div>
        </div>

        <div class="center-content home-center">
            <div class="home-heading">
                <text class="home-title">本地书籍</text>
                <text class="home-path">{{currentPath || '正在查找存储目录…'}}</text>
            </div>

            <div class="library-status" v-if="loading">
                <text class="status-text">正在读取本地文件…</text>
            </div>
            <div class="library-status" v-else-if="error">
                <text class="status-text">{{error}}</text>
            </div>
            <div class="library-status" v-else-if="entries.length === 0">
                <text class="status-text">此目录没有 TXT 书籍或子文件夹</text>
                <text class="status-hint">可使用右侧按钮刷新，或进入其他文件夹</text>
            </div>

            <div class="shelf-row" v-else>
                <div
                    class="file-card"
                    v-for="entry in entries"
                    :key="entry.path"
                    @click="openEntry(entry)"
                >
                    <text class="file-kind">{{entry.isDirectory ? '文件夹' : 'TXT 文本'}}</text>
                    <text class="file-title">{{entry.name}}</text>
                    <text class="file-meta">{{entry.isDirectory ? '打开文件夹' : entry.sizeLabel}}</text>
                </div>
            </div>
        </div>

        <div class="right-rail">
            <div class="right-action" @click="refresh">
                <text class="right-icon">⟳</text>
                <text class="right-label">刷新</text>
            </div>
            <div class="right-action" @click="goParent">
                <text class="right-icon">↑</text>
                <text class="right-label">上级</text>
            </div>
            <div class="right-action" @click="goStorageRoot">
                <text class="right-icon">⌂</text>
                <text class="right-label">存储</text>
            </div>
        </div>
    </div>
</template>

<script>
import { getFs } from '../../utils/FileSystem/FileSystem.js'
import { setCurrentBookPath } from '../../utils/BookSession.js'

const STORAGE_CANDIDATES = [
    '/sdcard',
    '/mnt/sdcard',
    '/storage/emulated/0',
    '/data'
]

function isTextFile(name) {
    return /\.txt$/i.test(String(name || ''))
}

function formatSize(size) {
    const value = Number(size) || 0
    if (value < 1024) return value + ' B'
    if (value < 1024 * 1024) return (value / 1024).toFixed(1) + ' KB'
    return (value / (1024 * 1024)).toFixed(1) + ' MB'
}

export default {
    name: 'index',
    data() {
        return {
            currentPath: '',
            entries: [],
            loading: false,
            error: ''
        }
    },
    onShow() {
        this.refresh()
    },
    methods: {
        async findStorageRoot(fs) {
            for (let i = 0; i < STORAGE_CANDIDATES.length; i++) {
                try {
                    if (await fs.exists(STORAGE_CANDIDATES[i])) {
                        const stat = await fs.stat(STORAGE_CANDIDATES[i])
                        if (stat && stat.isDirectory) return STORAGE_CANDIDATES[i]
                    }
                } catch (e) {}
            }
            return '/'
        },
        async refresh() {
            this.loading = true
            this.error = ''
            try {
                const fs = await getFs()
                if (!this.currentPath) {
                    this.currentPath = await this.findStorageRoot(fs)
                }
                const raw = await fs.readdir(this.currentPath, { withFileTypes: true })
                const result = []
                for (let i = 0; i < raw.length; i++) {
                    const item = raw[i]
                    const name = item.name || String(item)
                    if (!name || name.charAt(0) === '.') continue
                    const isDirectory = typeof item.isDirectory === 'function'
                        ? item.isDirectory()
                        : !!item.isDirectory
                    if (!isDirectory && !isTextFile(name)) continue
                    const path = item.path || (
                        this.currentPath.replace(/\/$/, '') + '/' + name
                    )
                    let sizeLabel = ''
                    if (!isDirectory) {
                        try {
                            const stat = await fs.stat(path)
                            sizeLabel = formatSize(stat && stat.size)
                        } catch (e) {
                            sizeLabel = 'TXT 文本'
                        }
                    }
                    result.push({ name, path, isDirectory, sizeLabel })
                }
                result.sort((a, b) => {
                    if (a.isDirectory !== b.isDirectory) return a.isDirectory ? -1 : 1
                    return a.name.localeCompare(b.name)
                })
                this.entries = result
            } catch (e) {
                this.entries = []
                this.error = '无法读取目录：' + (e && e.message ? e.message : String(e))
            } finally {
                this.loading = false
            }
        },
        openEntry(entry) {
            if (entry.isDirectory) {
                this.currentPath = entry.path
                this.refresh()
                return
            }
            setCurrentBookPath(entry.path)
            $falcon.navTo('reader')
        },
        goParent() {
            if (!this.currentPath || this.currentPath === '/') return
            const path = this.currentPath.replace(/\/+$/, '')
            const index = path.lastIndexOf('/')
            this.currentPath = index <= 0 ? '/' : path.slice(0, index)
            this.refresh()
        },
        goStorageRoot() {
            this.currentPath = ''
            this.refresh()
        },
        goBack() {
            if (this.currentPath && this.currentPath !== '/') {
                this.goParent()
                return
            }
            this.$page.finish()
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';
</style>
