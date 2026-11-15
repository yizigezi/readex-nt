<template>
    <div class="home-page">
        <div class="home-header">
            <text class="brand-label">READEX  ·  YOUR READING SPACE</text>
            <text class="page-title">读点喜欢的。</text>
            <text class="page-subtitle">让每一段空闲，都有好故事。</text>
        </div>

        <div class="continue-card" @click="openReader">
            <text class="card-kicker">READING EXPERIENCE</text>
            <text class="continue-title">沉浸式阅读</text>
            <text class="continue-description">为 X5 细长屏幕重新设计的阅读界面。更舒展的排版，更清晰的层次。</text>
            <text class="card-meta">打开阅读界面预览  →</text>
        </div>

        <text class="section-heading">我的书架</text>
        <div class="library-empty">
            <div class="empty-mark">
                <text>R</text>
            </div>
            <text class="empty-title">书架还没有书</text>
            <text class="empty-description">本地书籍导入和书架管理将在下一阶段接入。你的阅读空间，先从这里开始。</text>
        </div>

        <div class="home-footer">
            <text class="footer-caption">READEX  ·  MATERIAL 3 EXPRESSIVE</text>
        </div>
    </div>
</template>

<script>
import router from '@system.router'

export default {
    name: 'index',
    methods: {
        openReader() {
            router.push({
                uri: '/reader'
            })
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';
</style>
