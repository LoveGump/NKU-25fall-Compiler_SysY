# NKU-Compilers2025

## 注意事项（学术诚信问题）

在基于本框架完成实验时，可以参考开源仓库中的代码，但必须在注释中明确注明自己哪部分代码是参考的其他仓库，并给出自己参考仓库的具体链接，文件以及哪一行代码。如果参考了公开源代码，但是没有在注释中明确注明，将按抄袭处理，当此作业记0分，下面是一个注释的例子。

```cpp
// Reference: https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/Scalar/LICM.cpp line1-line37
```

## 禁止事项

下面列举了一些禁止事项，一旦发现有这些行为，当次实验按0分处理

1. 篡改测试样例来通过测试

2. 特判文件名，函数名，变量名等方式直接根据文件名生成对应的目标代码

3. 特判测试样例输入和输出来通过测试

4. 对一整个函数进行规模较大的模式匹配后直接生成优化后的ir或目标代码(如当函数有11个基本块，且第一个基本块指令数为5条......时使用某种优化)

5. 其他试图获取不公平优势的行为

如果你不清楚什么级别的模式匹配是合法的，下面有一些例子。

合理的模式匹配:

1. 模式匹配数组循环赋值，替换为memset

2. 模式匹配popcount等位运算操作，直接换为汇编指令或者库函数

禁止的模式匹配:

1. 通过判断一个函数有多少个参数，返回值，有多少个基本块，每个基本块从头到尾有什么指令的方式来模式匹配某一个函数，再将该函数替换为人工优化好的算法

## 运行测试

### 1.编译

```bash
# 编译你的代码，链接生成可执行文件compiler
make 
# 清除编译产物
make clean
```

### 2.运行你的样例程序

```bash
# 编译出的可执行文件位于./bin目录下，命名为compiler
# 在项目根目录下执行下列命令
./bin/compiler -lexer -o "output_filename" [-O0] "input_filename" #词法分析
./bin/compiler -parser -o "output_filename" [-O0] "input_filename" #语法分析
./bin/compiler -llvm -o "output_filename" [-O0|-O1|-O2|-O3] "input_filename" #中间代码生成
./bin/compiler -S -o "output_filename" [-O0|-O1|-O2|-O3] "input_filename" #目标代码生成

# 使用gdb调试编译器，以词法分析为例
gdb --args ./bin/compiler -lexer -o "output_filename" [-O0] "input_filename"

# [-Ox]表示代码优化阶段的优化级别
```

### 3.批量测试

命令行格式:

```bash
python3 test.py --group [Basic|Advanced] --stage [llvm|riscv|arm] --opt [0|1|2] 
```

以测试中间代码生成的基础要求，选择优化级别0为例，测试命令为：

```python3 test.py --group Basic --stage llvm --opt 0```

## Lab1. 词法分析

需要阅读的代码：

- main.cpp : 主函数，你需要了解框架的整体流程以及全局变量, 后续每次实验都需要进行阅读或者编写
- frontend/parser/scanner.h : 定义了词法分析的扫描器类
- frontend/parser/parser.h & parser.cpp : 这部分代码描述了词法分析的流程，有助于帮助你完整理解词法分析的工作原理
- frontend/parser/yacc.y : 只需要阅读开头%token的定义即可，你在词法分析中需要return的枚举类型均来自于该文件开头定义的%token
- interfaces/frontend/token.h : 定义了Token类


需要阅读并编写的代码：

- frontend/parser/yacc.y: 补充你需要使用的 token 声明
- frontend/parser/lexer.l : 编写你想实现的词法正则表达式及对应处理函数

Makefile 中默认选项会默认根据 yacc.y, lexer.l 来更新实现文件（如有更新），在完成 Lab2 前你会注意到 bison 会输出十分多的警告。我们此时暂且忽略它。

完成代码后，以-lexer运行编译器，如果能正确输出lexer测试用例中的每个token，即完成lab1。

## Lab2. 语法分析

需要阅读的代码：

- interfaces/frontend/symbol/symbol_entry.h: 符号表项定义。Lab2 中我们暂时还不需要实现符号表，但已经可以开始为一些符号名构建符号表项了。
- frontend/ast/（不含/visitor） ： 分类定义了语法树的节点类
- frontend/ast/visitor/printer/ ： 定义了语法树的打印过程，了解即可

__注意：同样的，你只需要将绝大多数注意力放在yacc.y上。__

__同样，如果你没有找到lexer/yacc.h/cpp，这是完全正常的，你只需要使用Makefile进行生成即可。__

需要阅读并编写的代码：

- frontend/parser/yacc.y : 编写你想要实现的文法定义以及对应的处理函数, 本次实验中只需要构建出语法树即可，不需要其他的额外处理

完成代码后，以-parser运行编译器，如果能正确输出parser测试用例中对应的语法树，即完成lab2。

## Lab3-1. 语义分析

需要阅读的代码：

- interfaces/frontend/symbol/isymbol_table.h ： 定义了符号表的操作接口。

需要阅读并编写的代码：

- frontend/symbol/symbol_table.cpp ：实现符号表的处理函数。
- frontend/ast/visitor/sementic_check/ : 结合注释，完成语义检查器的功能。

完成代码后，你的编译器应能对semant测试用例中的非法程序进行识别和报错；对于合法程序，以-parser运行编译器，应能打印出标记后的语法树。

## Lab3-2. 中间代码生成

需要阅读的代码：

- middleend/module/ : 定义了中间代码的操作数、指令、基本块、函数、模块类，你需要理解关键的成员变量和函数实现，这对你构建中间代码至关重要。
- middleend/visitor/printer/ : 中间代码的打印。另外，middleend/module/ir_instruction.cpp中给出了ir指令的具体打印过程，如果你不理解Lab3-2具体是在做什么，可以先阅读这个文件。

需要阅读并编写的代码：

- middleend/visitor/codegen/ ： 根据提示完成AST到IR的转换。注意，ast_codegen.h中已经为你提供了生成指令、插入基本块等操作的接口。

你可以用-llvm运行编译器进行打印和分析；完成代码后，运行testcase/functional/下的测试用例，根据测试用例的通过情况来对你的实现进行评分。

## Lab4. 中间代码优化

需要阅读的代码：

- interfaces/middleend/pass.h : 优化Pass基类定义，后续创建新的pass均继承于它。
- middleend/pass/analysis/analysis_manager.{h,cpp} : 中端分析管理器的类定义。
- middleend/pass/analysis/cfg.{h,cpp} : 控制流图的类定义。控制流信息在中端优化中广泛使用。
- middleend/pass/unify_return.{h,cpp} : 一个优化Pass示例。

需要阅读并编写的代码：

- middleend/pass/analysis/dominfo.{h.cpp} ：支配信息的类定义，你在进行mem2reg优化时需要建立支配树，完善此类。
- 如果你要编写新的优化Pass，请在middleend/pass/目录下新建文件，参照unify_return_pass的方式，自行设计算法。
- main.cpp ：在编译器主流程中执行你自行设计的优化Pass。

完成实验后，你的编译器生成的中间代码应符合优化后的模式；同时要保证testcase/functional/下原有的测试用例仍然正确。
