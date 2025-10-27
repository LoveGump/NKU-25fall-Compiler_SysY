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
INPUT_DIR="testcasemy/lexer"
OUTPUT_DIR="testcaseout/testcase/lexer"
BIN="./bin/compiler"

mkdir -p "$OUTPUT_DIR"

# 统计结果
total=0
passed=0

echo "=============================================="
echo "           Lexer Testing Started"
echo "=============================================="

for input_file in "$INPUT_DIR"/*.sy; do
    ((total++))

    filename=$(basename -- "$input_file")
    basename_no_ext="${filename%.*}"

    expected_file="$INPUT_DIR/$basename_no_ext.lexer"
    output_file="$OUTPUT_DIR/$basename_no_ext.lexer"

    echo -n "Testing $filename ... "

    # 执行你的程序
    $BIN -lexer -o "$output_file" $OPT_FLAG "$input_file" > /dev/null 2>&1

    # 判断结果文件是否存在
    if [ ! -f "$output_file" ]; then
        echo "❌ Output file missing!"
        continue
    fi

    # Diff对比
    if diff -q "$expected_file" "$output_file" > /dev/null; then
        echo "✅ Passed"
        ((passed++))
    else
        echo "❌ Failed"
        echo "  - Expected: $expected_file"
        echo "  - Output:   $output_file"
    fi
done

echo
echo "=============================================="
echo "           Test Results"
echo "=============================================="
echo "Total: $total, Passed: $passed, Failed: $((total - passed))"
echo "=============================================="

if [ $passed -eq $total ]; then
    echo "🎉 All tests passed!"
else
    echo "⚠ 有测试失败，请检查输出差异！"
fi