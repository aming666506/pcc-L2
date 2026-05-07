#!/bin/bash

# 开启错误检查：如果任何命令失败，脚本立即停止
set -e

echo ">>> Step 1: Setting up build environment..."
# 执行 meson 设置命令
meson setup build

echo ">>> Step 2: Compiling project..."
# 执行 meson 编译命令
meson compile -C build

# 打印当前目录路径以确认
pwd
