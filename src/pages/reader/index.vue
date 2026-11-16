<template>
    <div class="reader-page">
        <div class="left-rail">
            <div class="left-rail-top" @click="goBack">
                <text class="rail-back">‹</text>
            </div>
            <div class="rail-actions">
                <div class="rail-action">
                    <text class="rail-icon">☰</text>
                    <text class="rail-label">目录</text>
                </div>
            </div>
        </div>

        <div class="center-content reader-center">
            <text class="reader-text">{{pageText}}</text>
        </div>

        <div class="right-rail">
            <div class="right-action">
                <text class="right-icon">Aa</text>
                <text class="right-label">字号</text>
            </div>
            <div class="right-action">
                <text class="right-icon">☼</text>
                <text class="right-label">亮度</text>
            </div>
            <div class="right-action">
                <text class="right-icon">◐</text>
                <text class="right-label">主题</text>
            </div>
            <div class="right-action">
                <text class="right-icon">•••</text>
                <text class="right-label">更多</text>
            </div>
        </div>
    </div>
</template>

<script>
// X5 横屏设计画布：800 × 254 px。
// 左右功能栏各约 64px，正文区域约 672px。
const CONTENT_WIDTH = 636
const CONTENT_HEIGHT = 224

const SAMPLE_TEXT =
    '第一章 纸页之间\n\n' +
    '阅读，是在忙碌的日子里，为自己留下一小段安静的时间。故事从一行文字开始，在脑海中慢慢铺展开来。\n\n' +
    '窗外的风轻轻吹过，书页停留在熟悉的段落。那些曾经匆匆略过的句子，在重新读到时，忽然有了新的意味。\n\n' +
    '有时我们读一个人的经历，有时我们读一座城市的变化，也有时，只是想知道下一页会发生什么。文字不必总是给出答案，它也可以陪伴我们思考。\n\n' +
    '这是一段用于检查中文排版的示例内容。界面会根据屏幕可用宽度进行分页，并尽量让段落、标点和中英文混排保持自然。\n\n' +
    'Reading should feel calm and comfortable. Mixed language, numbers like 2026, and punctuation should remain clear on this display.\n\n' +
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
            pageText: SAMPLE_TEXT
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
            if (!Readex || typeof Readex.paginate !== 'function') return

            try {
                const pages = Readex.paginate(SAMPLE_TEXT, {
                    width: CONTENT_WIDTH,
                    height: CONTENT_HEIGHT,
                    fontSize: 17,
                    lineHeight: 1.55,
                    paddingTop: 0,
                    paddingBottom: 0,
                    paddingLeft: 0,
                    paddingRight: 0,
                    paragraphSpacing: 8
                })
                if (pages && pages.length) {
                    const first = pages[0]
                    this.pageText = SAMPLE_TEXT.substring(
                        jsIndexForByte(SAMPLE_TEXT, first.startOffset),
                        jsIndexForByte(SAMPLE_TEXT, first.endOffset)
                    )
                }
            } catch (e) {
                // 保留完整示例文本，避免分页模块异常影响阅读页显示。
            }
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';
</style>
