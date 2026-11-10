#!/bin/bash
# 构建生产包并安装到词典笔（需已连接 adb）
set -e

npm run build:prod
amr_file=$(ls *.amr | head -n1)
base_name="${amr_file%%.*}"

adb push "$amr_file" /userdisk/Favorite/
adb shell "miniapp_cli install /userdisk/Favorite/$amr_file"
adb shell "miniapp_cli start $base_name"
rm -f *.amr
