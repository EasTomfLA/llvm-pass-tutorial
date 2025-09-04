#!/bin/bash

# 设置错误处理
set -e
trap 'echo "错误：在第 $LINENO 行执行失败"; exit 1' ERR

# 功能1：编译 Hikari 库
function build_hikari() {
    echo "====== 开始执行：编译 Hikari 库 ======"
    
    rm -f ./output/libHikari.so
    
    # 确保build目录存在
    mkdir -p ./output/
    mkdir -p ./build/

    # 进入构建目录，清理、配置、编译
    cd build
    
    # 尝试make clean，但如果失败则不终止脚本
    # make clean || echo "警告: make clean 失败，可能是首次编译"
    
    # 继续编译流程
    cmake .. && make

    # 处理编译产物
    if [ -f ./Hikari/libHikari.so ]; then
        cp ./Hikari/libHikari.so ../output/ && echo "已复制 libHikari.so 到output目录"
    else
        echo "警告: ./Hikari/libHikari.so 不存在"
        # 尝试查找其他可能的文件位置
        hikari_so=$(find . -name "*Hikari*.so" | head -1)
        if [ -n "$hikari_so" ]; then
            cp "$hikari_so" ../output/ && echo "已复制 $hikari_so 到output目录"
        else
            echo "错误: 编译可能失败，未找到任何 Hikari 相关的 .so 文件"
            return 1
        fi
    fi

    # 返回原目录
    cd - >/dev/null

    # 验证文件是否存在
    if [ -f ./output/libHikari.so ]; then
        echo "验证成功: libHikari.so 现在位于项目output目录"
    else
        echo "验证失败: 项目output目录下未找到 libHikari.so"
        ls -la ./output/*.so 2>/dev/null || echo "项目output目录下没有任何 .so 文件"
        return 1
    fi
    
    echo "====== Hikari 库编译完成 ======"
    return 0
}

# 功能2：使用 Hikari 编译 mem_watcher
function build_mem_watcher() {
    echo "====== 开始执行：编译 mem_watcher ======"

    rm -f ./output/mem_obf
    
    # 检查依赖文件是否存在
    if [ ! -f ./output/libHikari.so ]; then
        echo "错误: libHikari.so 不存在，请先执行 build_hikari 编译该库"
        return 1
    fi
    
    # 检查 NDK 路径是否存在
    NDK_PATH="../android-ndk-r25c-linux/android-ndk-r25c"
    if [ ! -d "$NDK_PATH" ]; then
        echo "错误: Android NDK 路径不存在: $NDK_PATH"
        return 1
    fi
    
    # 执行编译
    $NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
        --static \
        -target aarch64-linux-android29 \
        ./testsrc/mem_watcher.cpp \
        -o ./output/mem_obf \
        -fpass-plugin=./output/libHikari.so
        
    # 验证编译结果
    if [ -f ./output/mem_obf ]; then
        echo "编译成功: mem_obf 文件已生成"
    else
        echo "编译失败: 未生成 mem_obf 文件"
        return 1
    fi
    
    echo "====== mem_watcher 编译完成 ======"
    return 0
}

# 显示帮助信息
function show_help() {
    echo "使用方法: $0 <功能>"
    echo "功能选项:"
    echo "  build_hikari      编译 Hikari 库"
    echo "  build_mem_watcher 使用 Hikari 编译 mem_watcher"
    echo "  all               顺序执行所有功能"
    echo "  help              显示此帮助信息"
}

# 主函数
function main() {
    case "$1" in
        build_hikari)
            build_hikari
            ;;
        build_mem_watcher)
            build_mem_watcher
            ;;
        all)
            build_hikari && build_mem_watcher
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            echo "错误: 未知的功能选项: $1"
            show_help
            exit 1
            ;;
    esac
}

# 参数检查
if [ $# -eq 0 ]; then
    echo "错误: 未指定功能参数"
    show_help
    exit 1
fi

# 执行主函数
main "$1"