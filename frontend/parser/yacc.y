%skeleton "lalr1.cc"
%require "3.2"

%define api.namespace { FE }
%define api.parser.class { YaccParser } // 生成的语法分析类名字是：FE::YaccParser
%define api.token.constructor
%define api.value.type variant
%define parse.assert
%defines

%code requires
{
    #include <memory>
    #include <string>
    #include <sstream>
    #include <frontend/ast/ast_defs.h>
    #include <frontend/ast/ast.h>
    #include <frontend/ast/stmt.h>
    #include <frontend/ast/expr.h>
    #include <frontend/ast/decl.h>
    #include <frontend/symbol/symbol_entry.h>
    

    namespace FE
    {
        class Parser;
        class Scanner;
    }
}

%code top // Bison ←→ 调用 yylex() ←→ 调用 Flex 扫描器。
{
    #include <iostream>

    #include <frontend/parser/parser.h>
    #include <frontend/parser/location.hh>
    #include <frontend/parser/scanner.h>
    #include <frontend/parser/yacc.h>

    using namespace FE;
    using namespace FE::AST;

    static YaccParser::symbol_type yylex(Scanner& scanner, Parser &parser)
    {
        (void)parser;
        return scanner.nextToken(); 
    }

    extern size_t errCnt;
}

%lex-param { FE::Scanner& scanner }
%lex-param { FE::Parser& parser }
%parse-param { FE::Scanner& scanner }
%parse-param { FE::Parser& parser }

%locations

%define parse.error verbose
%define api.token.prefix {TOKEN_}

// 从这开始定义你需要用到的 token
// 对于一些需要 "值" 的 token，可以在前面加上 <type> 来指定值的类型
// 例如，%token <int> INT_CONST 定义了一个名为 INT_CONST
// 整型
%token <int> INT_CONST
// 字符串： 字符串常量、错误token、单行注释 
%token <std::string> STR_CONST ERR_TOKEN SLASH_COMMENT 
// 标识符
%token <std::string> IDENT 
// 关键字
%token IF ELSE FOR WHILE CONTINUE BREAK SWITCH CASE GOTO DO RETURN CONST
// 界符 ;         ,     (      )      [        ]          {        }
%token SEMICOLON COMMA LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE
// 文件结束符
%token END

// 补充：
%token <long long> LL_CONST
// switch case default ：
%token DEFAULT COLON 
%token INT FLOAT VOID
// 多行注释
%token <std::string> MULTI_COMMENT  
// 浮点数字面量
%token <float> FLOAT_CONST
// 运算符 + - * / %
%token PLUS MINUS STAR SLASH MOD 
// 关系运算符 == != < > <= >=
%token EQ NE LT GT LE GE
// 逻辑运算符 && || !
%token AND OR NOT
// 自增自减 ++ --
%token INC DEC
// 赋值 =  += -= *= /= %=
%token ASSIGN PLUS_ASSIGN MINUS_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN

// 非终结符 EXPR 表示表达式 ；STMT 表示语句
%nterm <FE::AST::Operator> UNARY_OP
%nterm <FE::AST::Type*> TYPE
%nterm <FE::AST::InitDecl*> INITIALIZER
%nterm <std::vector<FE::AST::InitDecl*>*> INITIALIZER_LIST
%nterm <FE::AST::VarDeclarator*> VAR_DECLARATOR
%nterm <std::vector<FE::AST::VarDeclarator*>*> VAR_DECLARATOR_LIST
%nterm <FE::AST::VarDeclaration*> VAR_DECLARATION
%nterm <FE::AST::ParamDeclarator*> PARAM_DECLARATOR
%nterm <std::vector<FE::AST::ParamDeclarator*>*> PARAM_DECLARATOR_LIST

// 表达式相关非终结符 
// 表达式按优先级从低到高组织:
// EXPR(逗号) > ASSIGN(赋值) > LOGICAL_OR(||) > LOGICAL_AND(&&) >
// EQUALITY(==,!=) > RELATIONAL(<,>) > ADDSUB(+,-) > MULDIV(*,/) > UNARY(一元)
%nterm <FE::AST::ExprNode*> LITERAL_EXPR
%nterm <FE::AST::ExprNode*> BASIC_EXPR
%nterm <FE::AST::ExprNode*> FUNC_CALL_EXPR
%nterm <FE::AST::ExprNode*> UNARY_EXPR
%nterm <FE::AST::ExprNode*> MULDIV_EXPR
%nterm <FE::AST::ExprNode*> ADDSUB_EXPR
%nterm <FE::AST::ExprNode*> RELATIONAL_EXPR
%nterm <FE::AST::ExprNode*> EQUALITY_EXPR
%nterm <FE::AST::ExprNode*> LOGICAL_AND_EXPR
%nterm <FE::AST::ExprNode*> LOGICAL_OR_EXPR
%nterm <FE::AST::ExprNode*> ASSIGN_EXPR
%nterm <FE::AST::ExprNode*> NOCOMMA_EXPR
%nterm <FE::AST::ExprNode*> EXPR
%nterm <std::vector<FE::AST::ExprNode*>*> EXPR_LIST

%nterm <FE::AST::ExprNode*> ARRAY_DIMENSION_EXPR
%nterm <std::vector<FE::AST::ExprNode*>*> ARRAY_DIMENSION_EXPR_LIST
%nterm <FE::AST::ExprNode*> LEFT_VAL_EXPR

%nterm <FE::AST::StmtNode*> EXPR_STMT
%nterm <FE::AST::StmtNode*> VAR_DECL_STMT
%nterm <FE::AST::StmtNode*> BLOCK_STMT
%nterm <FE::AST::StmtNode*> FUNC_DECL_STMT
%nterm <FE::AST::StmtNode*> RETURN_STMT
%nterm <FE::AST::StmtNode*> WHILE_STMT
%nterm <FE::AST::StmtNode*> IF_STMT
%nterm <FE::AST::StmtNode*> BREAK_STMT
%nterm <FE::AST::StmtNode*> CONTINUE_STMT
%nterm <FE::AST::StmtNode*> FOR_STMT
%nterm <FE::AST::StmtNode*> FUNC_BODY
%nterm <FE::AST::StmtNode*> STMT

%nterm <std::vector<FE::AST::StmtNode*>*> STMT_LIST
%nterm <FE::AST::Root*> PROGRAM

//开始符号，program是整个语法分析的入口
%start PROGRAM

//THEN和ELSE用于处理if和else的移进-规约冲突
%precedence THEN
%precedence ELSE
// token 定义结束

%%

/*
语法分析：补全TODO(Lab2)处的文法规则及处理函数。
如果你不打算实现float、array这些进阶要求，可将对应部分删去。
*/

/*
 * ============================================================================
 * 1. 非终结符定义 (上面的第98-142行): 定义语法分析中的非终结符及其类型
 * 2. 文法规则 (下面的): 定义SysY语言的完整文法和语义动作
 * 
 * 工作原理:
 * - Bison使用LALR(1)自底向上分析算法
 * - 通过规约(reduce)和移进(shift)操作构建抽象语法树(AST)
 * - 每个文法规则对应一个语义动作，用于创建AST节点
 * 
 * 符号说明:
 * - $$: 当前规则归约后的语义值
 * - $1, $2, ...: 规则右部第1、2、...个符号的语义值
 * - @1, @2, ...: 规则右部第1、2、...个符号的位置信息
 * 
 * Lab2任务: 补全标记为TODO(Lab2)的文法规则，下面的文法部分
 * ============================================================================
 */

// 语法树匹配从这里开始
// 语法规则
PROGRAM:
    STMT_LIST {
        // 规约动作: 将语句列表包装成根节点
        // $1: std::vector<StmtNode*>* 类型，包含所有顶层语句
        // $$: Root* 类型，AST的根节点
        $$ = new Root($1);
        // 将构建好的AST保存到Parser对象中，供后续阶段使用
        parser.ast = $$;
    }
    | PROGRAM END {
        // 遇到文件结束符(EOF)，通知Bison接受输入并结束分析
        YYACCEPT;
    }
    ;

// -------- 语句列表 --------
// STMT_LIST: 由零个或多个语句组成的序列
// 使用左递归定义，避免右递归导致的栈溢出
// 左递归: A -> A α | β (先处理左边，再追加右边)

STMT_LIST:
    STMT {
        // 基础情况: 第一个语句
        // 创建一个新的vector来存储语句序列
        $$ = new std::vector<StmtNode*>();
        // 只有非空语句才加入列表（排除空语句和注释）
        if ($1) $$->push_back($1);
    }
    | STMT_LIST STMT {
         // 递归情况: 在已有的语句列表后追加新语句
        // $1: 已有的语句列表
        // $2: 新语句
        // 复用已有的vector，避免重新分配内存
        $$ = $1;
        // 追加新语句
        if ($2) $$->push_back($2);
    }
    ;

// -------- 语句分类 --------
// STMT: 所有语句类型的统一入口
// SysY语言支持的语句类型包括：
//   - 表达式语句: a = b + 3;
//   - 变量声明: int a = 5;
//   - 函数定义: int func() { ... }
//   - 控制流语句: if, for, while, break, continue, return
//   - 空语句: ;
//   - 注释: // ...
STMT:
    EXPR_STMT {
        // 表达式语句: 以分号结尾的表达式
        $$ = $1;
    }
    | VAR_DECL_STMT {
        // 变量声明语句: int a, b = 5;
        $$ = $1;
    }
    | FUNC_DECL_STMT {
         // 函数定义语句: int func(int a) { ... }
        $$ = $1;
    }
    | FOR_STMT {
        // for循环语句
        $$ = $1;
    }
    | IF_STMT {
        // if-else条件语句
        $$ = $1;
    }
    | CONTINUE_STMT {
        // continue语句: 跳过当前循环迭代
        $$ = $1;
    }
    | SEMICOLON {
        // 空语句: 单独的分号
        // 返回nullptr表示这不是一个有效的语句节点
        $$ = nullptr;
    }
    | SLASH_COMMENT {
        // 单行注释: // ...
        // 注释不生成AST节点，返回nullptr
        $$ = nullptr;
    }
    //TODO(Lab2)：考虑更多语句类型，大概可能有以下这些？
    // - BREAK_STMT: break语句，跳出循环
    // - RETURN_STMT: return语句，返回函数值
    // - WHILE_STMT: while循环
    // - BLOCK_STMT: 代码块 { ... }
    ;

//接下来是详细的具体语句
// continue: 用于循环中，跳过本次迭代的剩余代码，进入下一次迭代
CONTINUE_STMT:
    CONTINUE SEMICOLON {
        // 创建ContinueStmt节点
        // @1: CONTINUE关键字的位置信息（行号、列号）
        $$ = new ContinueStmt(@1.begin.line, @1.begin.column);
    }
    ;

// 表达式语句: 任何表达式后跟分号
// 如： a = b + 3;  func();  i++;
EXPR_STMT:
    EXPR SEMICOLON {
        // $1: 表达式节点 (ExprNode*)
        // 将表达式包装成语句节点
        $$ = new ExprStmt($1, @1.begin.line, @1.begin.column);
    }
    ;

// VAR_DECLARATION: 变量声明的核心规则（不含分号）
// 支持两种形式:
//   1. 普通变量声明: int a, b = 5, arr[10];
//   2. const常量声明: const float PI = 3.14;
VAR_DECLARATION:
    TYPE VAR_DECLARATOR_LIST {
        // 普通变量声明
        // $1: 类型 (Type*) - int/float/void
        // $2: 声明符列表 (vector<VarDeclarator*>*) - 变量名、数组维度、初始值
        // false: 表示非const
        $$ = new VarDeclaration($1, $2, false, @1.begin.line, @1.begin.column);
    }
    | CONST TYPE VAR_DECLARATOR_LIST {
        // const常量声明
        // $2: 类型
        // $3: 声明符列表
        // true: 表示const常量，后续语义分析需要检查const变量不能被修改
        $$ = new VarDeclaration($2, $3, true, @1.begin.line, @1.begin.column);
    }
    ;

// VAR_DECL_STMT: 完整的变量声明语句（含分号）
VAR_DECL_STMT:
    /* TODO(Lab2): Implement variable declaration statement rule */

    /* 
     * 规则: VAR_DECLARATION SEMICOLON
     *   VAR_DECLARATION SEMICOLON {
     *       // $1 是 VarDeclaration* 类型
     *       // 需要包装成 VarDeclStmt* 类型
     *       $$ = new VarDeclStmt($1, @1.begin.line, @1.begin.column);
     *   }
     */
    ;

// FUNC_BODY: 函数的代码块部分
// 可以是空的 {} 或包含语句的 { stmt1; stmt2; ... }
FUNC_BODY:
    LBRACE RBRACE {
        // 空函数体: void func() {}
        // 返回nullptr表示没有语句
        $$ = nullptr;
    }
    | LBRACE STMT_LIST RBRACE {
        // 包含语句的函数体
        // $2: 语句列表 (vector<StmtNode*>*)
        if (!$2 || $2->empty())
        {
            // 语句列表为空（可能只有注释或空语句）
            $$ = nullptr;
            delete $2;
        }
        else if ($2->size() == 1)
        {
            // 只有一条语句，不需要用BlockStmt包装
            // 直接使用该语句作为函数体
            $$ = (*$2)[0];
            delete $2;
        }
        // 多条语句，创建BlockStmt节点来表示代码块
        // BlockStmt会管理语句列表的生命周期
        else $$ = new BlockStmt($2, @1.begin.line, @1.begin.column);
    }
    ;

// FUNC_DECL_STMT: 函数定义语句
// 格式: 返回类型 函数名(参数列表) { 函数体 }
FUNC_DECL_STMT:
    TYPE IDENT LPAREN PARAM_DECLARATOR_LIST RPAREN FUNC_BODY {
         // $1: 返回类型 (Type*) - int/float/void
        // $2: 函数名 (string)
        // $4: 参数列表 (vector<ParamDeclarator*>*)
        // $6: 函数体 (StmtNode*)
        Entry* entry = Entry::getEntry($2);
        $$ = new FuncDeclStmt($1, entry, $4, $6, @1.begin.line, @1.begin.column);
    }
    ;

// 格式: for (初始化; 条件; 更新) 循环体
// 支持两种初始化方式:
//   1. 声明初始化: for (int i = 0; i < n; i++)
//   2. 表达式初始化: for (i = 0; i < n; i++)
FOR_STMT:
    FOR LPAREN VAR_DECLARATION SEMICOLON EXPR SEMICOLON EXPR RPAREN STMT {
        // 第一种: 声明初始化
        // $3: 初始化声明 (VarDeclaration*) - 如: int i = 0
        // $5: 条件表达式 (ExprNode*) - 如: i < n
        // $7: 更新表达式 (ExprNode*) - 如: i++ 或 i = i + 1
        // $9: 循环体语句 (StmtNode*)
        
        // 将声明包装成语句
        VarDeclStmt* initStmt = new VarDeclStmt($3, @3.begin.line, @3.begin.column);
        $$ = new ForStmt(initStmt, $5, $7, $9, @1.begin.line, @1.begin.column);
    }
    | FOR LPAREN EXPR SEMICOLON EXPR SEMICOLON EXPR RPAREN STMT {
        // 第二种: 表达式初始化
        // $3: 初始化表达式 (ExprNode*) - 如: i = 0
        
        // 将表达式包装成语句
        StmtNode* initStmt = new ExprStmt($3, $3->line_num, $3->col_num);
        $$ = new ForStmt(initStmt, $5, $7, $9, @1.begin.line, @1.begin.column);
    }
    ;

// IF_STMT: if-else条件分支
IF_STMT:
    /* TODO(Lab2): Implement if statement rule */
     /* 
     * 
     * 需要支持两种形式:
     * 1. if语句（无else）: if (条件) 语句
     * 2. if-else语句: if (条件) 语句 else 语句
     * 
     * 悬空else问题:
     *   if (a) if (b) stmt1; else stmt2;
     *   应该理解为: if (a) { if (b) stmt1; else stmt2; }
     *   而不是: if (a) { if (b) stmt1; } else stmt2;
     * 
     * 解决方法: 使用 %prec THEN 和 %prec ELSE
     * 
     * 
     *   IF LPAREN EXPR RPAREN STMT %prec THEN {
     *       // 无else分支的if语句
     *       // $3: 条件表达式
     *       // $5: then分支的语句
     *       $$ = new IfStmt($3, $5, nullptr, @1.begin.line, @1.begin.column);
     *   }
     *   | IF LPAREN EXPR RPAREN STMT ELSE STMT {
     *       // 有else分支的if语句
     *       // $7: else分支的语句
     *       $$ = new IfStmt($3, $5, $7, @1.begin.line, @1.begin.column);
     *   }
     */
    ;

//TODO(Lab2)：按照你补充的语句类型，实现这些语句的处理
// - BREAK_STMT: BREAK SEMICOLON { $$ = new BreakStmt(...); }
// - RETURN_STMT: RETURN EXPR SEMICOLON 或 RETURN SEMICOLON
// - WHILE_STMT: WHILE LPAREN EXPR RPAREN STMT
// - BLOCK_STMT: LBRACE STMT_LIST RBRACE (已在FUNC_BODY中实现类似逻辑)


//以下为函数参数相关的规则

// -------- 单个函数参数 --------
// PARAM_DECLARATOR: 函数形参声明
// 支持普通参数和数组参数
PARAM_DECLARATOR:
    TYPE IDENT {
        // 普通参数: int a, float x
        // $1: 参数类型
        // $2: 参数名
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, nullptr, @1.begin.line, @1.begin.column);
    }
    | TYPE IDENT LBRACKET RBRACKET {
        // 一维数组参数: int arr[], float data[]
        // 数组作为参数时，第一维的大小可以省略
        
        std::vector<ExprNode*>* dim = new std::vector<ExprNode*>();
        dim->emplace_back(new LiteralExpr(-1, @3.begin.line, @3.begin.column));
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, dim, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2)：考虑函数形参更多情况
    //我想到的： 支持多维数组参数
    // 示例: TYPE IDENT LBRACKET RBRACKET LBRACKET NOCOMMA_EXPR RBRACKET
    // 表示: int arr[][10] (第一维可省略，后续维度必须指定)
    ;

// PARAM_DECLARATOR_LIST: 函数的所有形参，参数列表
// 可以是空列表（无参函数）或多个参数（用逗号分隔）
PARAM_DECLARATOR_LIST:
    /* empty */ {
        // 无参数: void func()
        // 创建空的参数列表
        $$ = new std::vector<ParamDeclarator*>();
    }
    //TODO(Lab2)：考虑函数形参列表的构成情况
    // | PARAM_DECLARATOR {
    //     // 单个参数: int func(int a)
    //     $$ = new std::vector<ParamDeclarator*>();
    //     $$->push_back($1);
    // }
    // | PARAM_DECLARATOR_LIST COMMA PARAM_DECLARATOR {
    //     // 多个参数: int func(int a, float b)
    //     $$ = $1;
    //     $$->push_back($3);
    // }
    ;


//下面是变量声明相关规则


// VAR_DECLARATOR: 单个变量的声明
// 包含变量名、数组维度（如果是数组）、初始值（如果有初始化）
VAR_DECLARATOR:
    //TODO(Lab2)：完成变量声明符的处理
    // 需要支持多种情况:
    // 
    // 1. 简单变量: int a;
    //    IDENT {
    //        Entry* entry = Entry::getEntry($1);
    //        ExprNode* lval = new LeftValExpr(entry, nullptr, ...);
    //        $$ = new VarDeclarator(lval, nullptr, ...);
    //    }
    // 
    // 2. 带初始化: int a = 5;
    //    IDENT ASSIGN INITIALIZER {
    //        Entry* entry = Entry::getEntry($1);
    //        ExprNode* lval = new LeftValExpr(entry, nullptr, ...);
    //        $$ = new VarDeclarator(lval, $3, ...);
    //    }
    // 
    // 3. 数组: int arr[10];
    //    IDENT ARRAY_DIMENSION_EXPR_LIST {
    //        Entry* entry = Entry::getEntry($1);
    //        ExprNode* lval = new LeftValExpr(entry, $2, ...);
    //        $$ = new VarDeclarator(lval, nullptr, ...);
    //    }
    // 
    // 4. 数组带初始化: int arr[3] = {1, 2, 3};
    //    IDENT ARRAY_DIMENSION_EXPR_LIST ASSIGN INITIALIZER
    ;

// VAR_DECLARATOR_LIST: 同一声明中的多个变量
// 示例: int a, b = 5, arr[10];
VAR_DECLARATOR_LIST:
    VAR_DECLARATOR {
        // 第一个声明符
        $$ = new std::vector<VarDeclarator*>();
        $$->push_back($1);
    }
    | VAR_DECLARATOR_LIST COMMA VAR_DECLARATOR {
        // 追加更多声明符（用逗号分隔）
        $$ = $1;
        $$->push_back($3);
    }
    ;


//初始化模块
// INITIALIZER: 变量的初始值
// 可以是单个表达式或初始化列表
INITIALIZER:
    /* TODO(Lab2): Implement variable initializer rule */
     /* TODO(Lab2): 实现初始化器规则
     * 
     * 需要支持:
     * 1. 单个表达式: int a = 5 + 3;
     *    NOCOMMA_EXPR {
     *        $$ = new Initializer($1, @1.begin.line, @1.begin.column);
     *    }
     * 
     * 2. 初始化列表: int arr[] = {1, 2, 3};
     *    LBRACE INITIALIZER_LIST RBRACE {
     *        $$ = new InitializerList($2, @1.begin.line, @1.begin.column);
     *    }
     * 
     * 3. 空初始化列表: int arr[] = {};
     *    LBRACE RBRACE {
     *        auto empty = new std::vector<InitDecl*>();
     *        $$ = new InitializerList(empty, @1.begin.line, @1.begin.column);
     *    }
     */
    ;

// INITIALIZER_LIST: 用于数组的初始化列表，其实和前面的多参数很像，都是第一个加追加。
// 格式: {init1, init2, init3, ...}
// 支持嵌套: {{1,2}, {3,4}} for int arr[2][2]
INITIALIZER_LIST:
    INITIALIZER {
        $$ = new std::vector<InitDecl*>();
        $$->push_back($1);
    }
    | INITIALIZER_LIST COMMA INITIALIZER {
        $$ = $1;
        $$->push_back($3);
    }
    ;


//表达式相关

// 表达式按运算符优先级从低到高组织:
// EXPR (逗号) > ASSIGN (赋值) > LOGICAL_OR (||) > LOGICAL_AND (&&) >
// EQUALITY (==,!=) > RELATIONAL (<,>,<=,>=) > ADDSUB (+,-) > 
// MULDIV (*,/,%) > UNARY (一元) > BASIC (基本)

// ASSIGN_EXPR: 赋值运算
// 支持: =, +=, -=, *=, /=, %=
// 特点: 右结合，a = b = c 解析为 a = (b = c)
ASSIGN_EXPR:
    // TODO(Lab2): 完成赋值表达式的处理
    // 1. 简单赋值: a = 5
    //    LEFT_VAL_EXPR ASSIGN NOCOMMA_EXPR {
    //        $$ = new AssignExpr(Operator::ASSIGN, $1, $3, ...);
    //    }
    // 
    // 2. 复合赋值: a += 5 (等价于 a = a + 5)
    //    LEFT_VAL_EXPR PLUS_ASSIGN NOCOMMA_EXPR {
    //        $$ = new AssignExpr(Operator::ADD_ASSIGN, $1, $3, ...);
    //    }
    // 
    // 其他复合赋值: -=, *=, /=, %=
    // 对应: SUB_ASSIGN, MUL_ASSIGN, DIV_ASSIGN, MOD_ASSIGN
    ;



// EXPR_LIST: 用于函数调用的参数列表，列表类都很像，第一个和追加
// 注意: 这里用NOCOMMA_EXPR，因为逗号用于分隔参数，不是逗号运算符
// 示例: func(a + b, c * d, e)
EXPR_LIST:
    NOCOMMA_EXPR {
        $$ = new std::vector<ExprNode*>();
        $$->push_back($1);
    }
    | EXPR_LIST COMMA NOCOMMA_EXPR {
        $$ = $1;
        $$->push_back($3);
    }
    ;

// EXPR: 最低优先级的表达式
// 逗号运算符: 从左到右求值，返回最后一个表达式的值
// 示例: (a = 1, b = 2, c = 3) 结果是3
EXPR:
    NOCOMMA_EXPR {
        // 单个表达式（无逗号运算符）
        $$ = $1;
    }
    | EXPR COMMA NOCOMMA_EXPR {
        // 逗号表达式: expr1, expr2
        // $1: 左侧表达式
        // $3: 右侧表达式
        if ($1->isCommaExpr()) {
            CommaExpr* ce = static_cast<CommaExpr*>($1);
            ce->exprs->push_back($3);
            $$ = ce;
        } else {
            // $1不是CommaExpr，创建新的CommaExpr
            auto vec = new std::vector<ExprNode*>();
            vec->push_back($1);
            vec->push_back($3);
            $$ = new CommaExpr(vec, $1->line_num, $1->col_num);
        }
    }
    ;

// NOCOMMA_EXPR: 除逗号运算符外的所有表达式
// 用于不允许逗号运算符的地方:
//   - 函数参数: func(a, b) 这里的逗号是参数分隔符
//   - 数组下标: arr[i, j] 在SysY中不合法
//   - for循环的各部分
NOCOMMA_EXPR:
    LOGICAL_OR_EXPR {
        // 逻辑或及更高优先级的表达式
        $$ = $1;
    }
    | ASSIGN_EXPR {
        // 赋值表达式
        $$ = $1;
    }
    ;

// 运算符: ||
// 优先级: 低于 &&, 高于赋值
// 短路求值: 左操作数为true时，不计算右操作数
LOGICAL_OR_EXPR:
    /* TODO(Lab2): Implement logical OR expression rule */
    ;

// 运算符: &&
// 优先级: 低于相等运算, 高于逻辑或
// 短路求值: 左操作数为false时，不计算右操作数
LOGICAL_AND_EXPR:
    /* TODO(Lab2): Implement logical AND expression rule */
    ;

// 运算符: == !=
// 优先级: 低于关系运算, 高于逻辑与
// 返回: 布尔值（0或1）
EQUALITY_EXPR:
    /* TODO(Lab2): Implement equality expression rule */
    ;


// 运算符: < > <= >=
// 优先级: 低于加减, 高于相等
// 返回: 布尔值（0或1）
RELATIONAL_EXPR:
    /* TODO(Lab2): Implement relational expression rule */
    ;

// 运算符: + -
// 优先级: 低于乘除, 高于关系
// 左结合: a - b + c = (a - b) + c
ADDSUB_EXPR:
    /* TODO(Lab2): Implement addition and subtraction expression rule */
    ;

// 运算符: * / %
// 优先级: 低于一元, 高于加减
// 左结合: a / b * c = (a / b) * c
MULDIV_EXPR:
    /* TODO(Lab2): Implement multiplication and division expression rule */
    ;

// 运算符: + - ! (以及可选的 ++ --)
// 优先级: 最高（除了括号和函数调用）
// 右结合: --x 解析为 -(-x)
UNARY_EXPR:
    BASIC_EXPR {
        $$ = $1;
    }
    | UNARY_OP UNARY_EXPR {
        $$ = new UnaryExpr($1, $2, $2->line_num, $2->col_num);
    }
    ;


// BASIC_EXPR: 最高优先级的表达式单元
// 不可再分解的原子表达式
BASIC_EXPR:
    LITERAL_EXPR {
        $$ = $1;
    }
    | LEFT_VAL_EXPR {
        $$ = $1;
    }
    | LPAREN EXPR RPAREN {
        $$ = $2;
    }
    | FUNC_CALL_EXPR {
        $$ = $1;
    }
    ;


// -------- 函数调用表达式 --------
// FUNC_CALL_EXPR: 函数调用
// 支持有参和无参调用
// 特殊处理SysY的计时函数
FUNC_CALL_EXPR:
    IDENT LPAREN RPAREN {
        std::string funcName = $1;
        if (funcName != "starttime" && funcName != "stoptime")
        {
            Entry* entry = Entry::getEntry(funcName);
            $$ = new CallExpr(entry, nullptr, @1.begin.line, @1.begin.column);
        }
        else
        {    
            funcName = "_sysy_" + funcName;
            std::vector<ExprNode*>* args = new std::vector<ExprNode*>();
            args->emplace_back(new LiteralExpr(static_cast<int>(@1.begin.line), @1.begin.line, @1.begin.column));
            $$ = new CallExpr(Entry::getEntry(funcName), args, @1.begin.line, @1.begin.column);
        }
    }
    | IDENT LPAREN EXPR_LIST RPAREN {
        Entry* entry = Entry::getEntry($1);
        $$ = new CallExpr(entry, $3, @1.begin.line, @1.begin.column);
    }
    ;

// ARRAY_DIMENSION_EXPR: 单个数组下标
// 格式: [expr]
ARRAY_DIMENSION_EXPR:
    LBRACKET NOCOMMA_EXPR RBRACKET {
        $$ = $2;
    }
    ;

// ARRAY_DIMENSION_EXPR_LIST: 数组的所有维度
// 格式: [expr1][expr2][expr3]...
ARRAY_DIMENSION_EXPR_LIST:
    /* TODO(Lab2): Implement variable dimension rule */
    ;


// LEFT_VAL_EXPR: 可以出现在赋值号左边的表达式
// 包括: 变量、数组元素
LEFT_VAL_EXPR:
    IDENT {
        Entry* entry = Entry::getEntry($1);
        $$ = new LeftValExpr(entry, nullptr, @1.begin.line, @1.begin.column);
    }
    | IDENT ARRAY_DIMENSION_EXPR_LIST {
        Entry* entry = Entry::getEntry($1);
        $$ = new LeftValExpr(entry, $2, @1.begin.line, @1.begin.column);
    }
    ;

// LITERAL_EXPR: 常量值
// 包括: 整数、长整数、浮点数
LITERAL_EXPR:
    INT_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2): 处理更多字面量
    ;

// TYPE: 基本数据类型
// SysY支持: int, float, void
TYPE:
    // TODO(Lab2): 完成类型的处理
    ;

// UNARY_OP: 一元运算符到Operator枚举的映射
UNARY_OP:
    // TODO(Lab2): 完成一元运算符的处理
    ;

%%
// 文法规则结束

void FE::YaccParser::error(const FE::location& location, const std::string& message)
{
    // 输出错误信息到标准错误流
    // location: 错误发生的位置（文件名、行号、列号）
    // message: Bison自动生成的错误描述
    //   - "syntax error" : 通用语法错误
    //   - "unexpected XXX" : 遇到意外的Token
    //   - "expecting XXX" : 期望某个Token
    std::cerr << "msg: " << message << ", error happened at: " << location << std::endl;
}
