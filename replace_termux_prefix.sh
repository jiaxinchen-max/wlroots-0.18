#!/bin/bash

# 替换 @TERMUX_PREFIX@ 为实际的 Termux 前缀路径
# 使用方法: ./replace_termux_prefix.sh [TERMUX_PREFIX]
# 默认前缀: /data/data/com.termux/files/usr

TERMUX_PREFIX=${1:-"/data/data/com.termux/files/usr"}

echo "正在替换 @TERMUX_PREFIX@ 为: $TERMUX_PREFIX"

# 查找所有 C 和头文件，替换 @TERMUX_PREFIX@
find . -name "*.c" -o -name "*.h" | while read file; do
    if grep -q "@TERMUX_PREFIX@" "$file"; then
        echo "处理文件: $file"
        sed -i.bak "s|@TERMUX_PREFIX@|$TERMUX_PREFIX|g" "$file"
        # 删除备份文件
        rm -f "$file.bak"
    fi
done

echo "替换完成！"
echo "请确保 $TERMUX_PREFIX/tmp 目录存在且可写"