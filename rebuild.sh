o "当前路径: $(pwd)"

# 1. 检查并删除 build 文件夹
if [ -d "build" ]; then
	    echo "检测到 build 文件夹，正在删除..."
	        rm -rf build
	else
		    echo "未检测到 build 文件夹，跳过删除。"
fi

# 2. 执行 ./build
if [ -f "./build.sh" ]; then
	    echo "正在执行 ./build ..."
	        ./build.sh
	else
		    echo "错误：当前目录下找不到 ./build 脚本！"
		        return 2>/dev/null || exit 1
fi

# 3. 切换到 build 目录
if [ -d "build" ]; then
	    echo "正在进入 build 目录..."
	        cd build
		    echo "完成！当前路径: $(pwd)"
	    else
		        echo "错误：执行 ./build 后未生成 build 目录。"
fi

