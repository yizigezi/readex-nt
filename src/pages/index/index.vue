<template>
    <div class="home-page">
        <div class="left-rail">
            <div class="left-rail-top" @click="goBack">
                <text class="rail-back">‹</text>
            </div>
            <div class="rail-actions">
                <div class="rail-action rail-action-active">
                    <text class="rail-icon">▤</text>
                    <text class="rail-label">书架</text>
                </div>
                <div class="rail-action">
                    <text class="rail-icon">⌕</text>
                    <text class="rail-label">搜索</text>
                </div>
                <div class="rail-action">
                    <text class="rail-icon">⚙</text>
                    <text class="rail-label">设置</text>
                </div>
            </div>
        </div>

        <div class="center-content home-center">
            <div class="home-heading">
                <text class="home-title">我的书架</text>
                <text class="home-subtitle">READEX · 让阅读更简单</text>
            </div>
            <div class="shelf-row">
                <div class="feature-card" @click="openReader">
                    <text class="feature-kicker">READEX READER</text>
                    <text class="feature-title">沉浸式阅读</text>
                    <text class="feature-description">为横屏 X5 优化的阅读体验</text>
                    <text class="feature-link">打开阅读器　›</text>
                </div>
                <div class="empty-shelf-card">
                    <text class="empty-title">书架还是空的</text>
                    <text class="empty-description">本地书籍导入功能接入后，书籍会在这里横向排列。</text>
                </div>
            </div>
        </div>

        <div class="right-rail">
            <div class="right-action">
                <text class="right-icon">◐</text>
                <text class="right-label">主题</text>
            </div>
            <div class="right-action">
                <text class="right-icon">☼</text>
                <text class="right-label">亮度</text>
            </div>
            <div class="right-action">
                <text class="right-icon">Aa</text>
                <text class="right-label">字号</text>
            </div>
            <div class="right-action">
                <text class="right-icon">•••</text>
                <text class="right-label">更多</text>
            </div>
        </div>
    </div>
</template>

<script>
export default {
    name: 'index',
    methods: {
        openReader() {
            $falcon.navTo('reader')
        },
        goBack() {
            this.$page.finish()
        }
    }
}
</script>

<style lang="less">
@import '../../styles/common.less';
</style>
