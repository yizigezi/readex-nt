<template>
    <div class="reader-page">
        <div class="reader-header">
            <div class="reader-back" @click="goBack">
                <text>‹</text>
            </div>
            <div class="reader-heading">
                <text class="reader-heading-title">沉浸式阅读</text>
                <text class="reader-heading-subtitle">READEX  ·  排版预览</text>
            </div>
        </div>

        <div class="reader-content">
            <text class="reader-text">{{pageText}}</text>
        </div>

        <div class="reader-footer">
            <div class="reader-progress-row">
                <text class="reader-progress-label">{{status}}</text>
                <text class="reader-progress-label">示例章节</text>
            </div>
            <text class="reader-hint">轻触左上角返回书架 · 正式书籍接入后显示真实进度</text>
        </div>
    </div>
</template>

<script>
// X5 面板规格：254 × 800 px（2.97 英寸）。
// 阅读区域按真实屏幕宽度扣除左右 18px 内边距计算。
// Falcon 暂无稳定的节点布局测量 API，因此这里使用固定设计画布；
// 其他相近尺寸型号可在后续根据实际屏幕宽高做比例适配。
const SCREEN_WIDTH = 254
const CONTENT_WIDTH = SCREEN_WIDTH - 36
const CONTENT_HEIGHT = 596

const SAMPLE_TEXT =
    '第一章 纸页之间\n\n' +
    '阅读，是在忙碌的日子里，为自己留下一小段安静的时间。故事从一行文字开始，在脑海中慢慢铺展开来。\n\n' +
    '窗外的风轻轻吹过，书页停留在熟悉的段落。那些曾经匆匆略过的句子，在重新读到时，忽然有了新的意味。\n\n' +
    '有时我们读一个人的经历，有时我们读一座城市的变化，也有时，只是想知道下一页会发生什么。文字不必总是给出答案，它也可以陪伴我们思考。\n\n' +
    '这是一段用于检查中文排版的示例内容。界面会根据屏幕可用宽度进行分页，并尽量让段落、标点和中英文混排保持自然。\n\n' +
    'Reading should feel calm and comfortable. Mixed language, numbers like 2026, and punctuation should remain clear on this narrow display.\n\n' +
    '当你再次翻开一本书，愿你总能在字里行间，找到属于自己的节奏。'

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
    try {
        const mod = await import('readex')
        return mod && (mod.default || mod)
    } catch (e) {
        return null
    }
}

export default {
    name: 'reader',
    data() {
        return {
            pageText: SAMPLE_TEXT,
            status: '排版预览'
        }
    },
    onShow() {
        this.runPagination()
    },
    methods: {
        goBack() {
            this.$page.finish()
        },
        async runPagination() {
            const Readex = await loadReadex()
            if (!Readex || typeof Readex.paginate !== 'function') {
                this.status = '预览模式'
                return
            }

            try {
                const pages = Readex.paginate(SAMPLE_TEXT, {
                    width: CONTENT_WIDTH,
                    height: CONTENT_HEIGHT,
                    fontSize: 18,
                    lineHeight: 1.65,
                    paddingTop: 0,
                    paddingBottom: 0,
                    paddingLeft: 0,
                    paddingRight: 0,
                    paragraphSpacing: 10
                })
                if (pages && pages.length) {
                    const first = pages[0]
                    this.pageText = SAMPLE_TEXT.substring(
                        jsIndexForByte(SAMPLE_TEXT, first.startOffset),
                        jsIndexForByte(SAMPLE_TEXT, first.endOffset)
                    )
                    this.status = '第 1 页 / 共 ' + pages.length + ' 页'
                }
            } catch (e) {
                this.status = '排版预览'
            }
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';
</style>
