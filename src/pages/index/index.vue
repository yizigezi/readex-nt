<template>
    <div class="bar-screen">
        <!--
            毛坯（PR-1）：横屏条形屏一行式入口。
            按钮暂不跳转，页面路由与真实功能在后续 PR 接入。
        -->
        <div class="entry">
            <text class="entry-name">书架</text>
        </div>
        <div class="entry">
            <text class="entry-name">阅读</text>
        </div>
        <div class="entry">
            <text class="entry-name">设置</text>
        </div>
    </div>
</template>

<script>
export default {
    name: 'index'
}
</script>

<style lang="less">
@import '../../styles/common.less';
</style>
