<template>
    <div class="reader-page">
        <!--
            阅读页毛坯（PR-4）：最小阅读区占位。
            关键接线：可用阅读区域宽高【显式】传给 Readex.paginate（config.width/
            height），引擎不再从屏幕比例反推工具栏宽度（修「内容挤在屏幕左侧」的
            根子）。当前是毛坯级验证通路，不声称真机效果；真机验收（内容不再挤在
            左侧）由主人在 X5 上做。
        -->
        <div class="reader-area">
            <text class="reader-text">{{pageText}}</text>
        </div>
        <div class="reader-status">
            <text class="status-text">{{status}}</text>
        </div>
    </div>
</template>

<script>
// 阅读区像素尺寸的来源（PR-4 毛坯约定）：
//   falcon 目前没有暴露布局测量 API（拿不到节点的实际渲染宽高），这里先用
//   「屏幕常量 × 布局占比」换算出阅读区 px，传给 Readex.paginate。
//   **真机尺寸数据（或测量 API）到位后校准** SCREEN_* 常量与 AREA_*_RATIO，
//   并改为从真实布局测量取值；分页引擎侧无需改动（宽高本来就是显式入参）。

const SCREEN_W_PX = 400 // TODO 真机校准：横屏分辨率宽（X5 实测后填）
const SCREEN_H_PX = 240 // TODO 真机校准：横屏分辨率高
// 与下方 styles 的 .reader-area 布局占比保持一致（宽 100vw、高 85vh）
const AREA_W_RATIO = 1.0
const AREA_H_RATIO = 0.85

// 毛坯示例章文本（一章的已解码文本；真实流程来自 book.getChapter(i).text）
const SAMPLE_TEXT = '第一章 初见\n' +
    '这是一个分页引擎的示例段落，用于验证阅读区尺寸显式传入后的排版。' +
    '中英混排 mixed layout 测试：Hello, world! 42。\n' +
    '「引号不落行尾，句号不落行首。」标点避头尾的基础验证……\n' +
    '换页位置与阅读进度的对应关系将在 PR-5 由 ReadingState 持久化。'

// 偏移语义（见 native/include/readex/pagination.hpp 头注释）：
// PageInfo.startOffset / endOffset 是 content（UTF-8）字节偏移，恒在码点边界。
// JS 字符串下标 ≠ UTF-8 字节偏移（中文 1 字 = 3 字节），切片前需换算。
function jsIndexForByte(str, byteOff) {
    let bytes = 0
    let i = 0
    while (i < str.length && bytes < byteOff) {
        const c = str.charCodeAt(i)
        if (c < 0x80) bytes += 1
        else if (c < 0x800) bytes += 2
        else if (c >= 0xD800 && c < 0xDC00 && i + 1 < str.length) { bytes += 4; i++ }
        else bytes += 3
        i++
    }
    return i
}

// 原生模块走动态 import（同 src/utils/FileSystem 的模式）：静态 import 在
// 模块缺失时会让整个模块链求值失败（页面白屏），动态 import 可捕获降级。
async function loadReadex() {
    try {
        const mod = await import('readex')
        return mod && (mod.default || mod)
    } catch (e) {
        return null // PC 构建/未装原生模块：安静降级，页面仍可打开
    }
}

export default {
    name: 'reader',
    data: {
        pageText: '（分页结果将显示在这里）',
        status: '未分页'
    },
    onShow() {
        this.runPagination()
    },
    methods: {
        async runPagination() {
            const Readex = await loadReadex()
            if (!Readex) {
                this.status = '原生模块 readex 不可用（PC 构建属正常）'
                return
            }
            // 可用阅读区域宽高 = 真实布局占比 × 屏幕尺寸（显式传入，见文件头）
            const config = {
                width: SCREEN_W_PX * AREA_W_RATIO,
                height: SCREEN_H_PX * AREA_H_RATIO,
                fontSize: 20,
                lineHeight: 1.5
                // 其余字段（字距/段距/边距/估算系数）走默认值
            }
            try {
                const pages = Readex.paginate(SAMPLE_TEXT, config)
                const first = pages[0]
                this.pageText = SAMPLE_TEXT.substring(
                    jsIndexForByte(SAMPLE_TEXT, first.startOffset),
                    jsIndexForByte(SAMPLE_TEXT, first.endOffset))
                this.status = '共 ' + pages.length + ' 页（第 1 页预览）'
            } catch (e) {
                this.status = '分页失败: ' + (e && e.message ? e.message : e)
            }
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';

// 阅读区：占比与脚本里 AREA_*_RATIO 一致（真机校准两处一起改）
.reader-page {
    width: 100vw;
    height: 100vh;
    flex-direction: column;
    background-color: @surface;
}

.reader-area {
    width: 100vw;
    height: 85vh;
    padding: 2vh 2vw;
    background-color: @surface;
}

.reader-text {
    color: @on-surface;
    font-size: 4vh;
}

.reader-status {
    width: 100vw;
    height: 15vh;
    justify-content: center;
    align-items: center;
    background-color: @surface-variant;
}

.status-text {
    color: @on-surface-variant;
    font-size: 3vh;
}
</style>
