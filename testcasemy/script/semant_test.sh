#!/bin/bash

# 可选优化开关
OPT_FLAG=""
if [ "$1" == "-O0" ]; then
    OPT_FLAG="-O0"
    echo "Using -O0 optimization flag"
fi

# 编译项目
echo "Running make..."
if ! make; then
    echo "Make failed. Exiting."
    exit 1
fi

# 路径设置
INPUT_DIR="testcase/semant"
OUTPUT_DIR="testcaseout/testcase/semant"
BIN="./bin/compiler"

mkdir -p "$OUTPUT_DIR"

# 统计结果
total=0
saved=0

echo "=============================================="
echo "           Semant Testing Started"
echo "=============================================="

for input_file in "$INPUT_DIR"/*.sy; do
    ((total++))

    filename=$(basename -- "$input_file")
    basename_no_ext="${filename%.*}"

    # 保存终端输出到 .out 文件
    output_file="$OUTPUT_DIR/$basename_no_ext.out"

    echo -n "Running $filename ... "

    # 执行编译器并将终端输出重定向到文件（捕获 stdout 和 stderr）
    $BIN  $OPT_FLAG "$input_file" > "$output_file" 2>&1

    # 判断结果文件是否存在
    if [ -f "$output_file" ]; then
        echo "✅ Saved"
        ((saved++))
    else
        echo "❌ Failed to save output"
    fi
done

echo
echo "=============================================="
echo "           Test Results"
echo "=============================================="
echo "Total: $total, Saved: $saved, Failed: $((total - saved))"
echo "=============================================="

if [ $saved -eq $total ]; then
    echo "🎉 All outputs saved!"
else
    echo "⚠ 有输出未生成，请检查编译或脚本执行！"
fi