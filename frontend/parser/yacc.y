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

// token 定义结束

// 非终结符 EXPR 表示表达式 ；STMT 表示语句
// 一元运算符
%nterm <FE::AST::Operator> UNARY_OP 
// 类型
%nterm <FE::AST::Type*> TYPE 
// 变量类型（不允许 void 用于变量声明）
%nterm <FE::AST::Type*> VAR_TYPE
// 变量初始化
%nterm <FE::AST::InitDecl*> INITIALIZER
// 变量初始化列表
%nterm <std::vector<FE::AST::InitDecl*>*> INITIALIZER_LIST
// 变量声明符
%nterm <FE::AST::VarDeclarator*> VAR_DECLARATOR
// 变量声明符列表
%nterm <std::vector<FE::AST::VarDeclarator*>*> VAR_DECLARATOR_LIST
// 变量声明
%nterm <FE::AST::VarDeclaration*> VAR_DECLARATION
// 变量声明语句
%nterm <FE::AST::ParamDeclarator*> PARAM_DECLARATOR
// 函数形参列表
%nterm <std::vector<FE::AST::ParamDeclarator*>*> PARAM_DECLARATOR_LIST

// 表达式相关非终结符 
// 表达式按优先级从低到高组织:
// EXPR(逗号) > ASSIGN(赋值) > LOGICAL_OR(||) > LOGICAL_AND(&&) >
// EQUALITY(==,!=) > RELATIONAL(<,>) > ADDSUB(+,-) > MULDIV(*,/) > UNARY(一元)
%nterm <FE::AST::ExprNode*> LITERAL_EXPR
// 基础表达式
%nterm <FE::AST::ExprNode*> BASIC_EXPR
// 函数调用表达式
%nterm <FE::AST::ExprNode*> FUNC_CALL_EXPR
// 一元表达式
%nterm <FE::AST::ExprNode*> UNARY_EXPR
// * /
%nterm <FE::AST::ExprNode*> MULDIV_EXPR
// +-
%nterm <FE::AST::ExprNode*> ADDSUB_EXPR
// 关系表达式：  < > <= >=
%nterm <FE::AST::ExprNode*> RELATIONAL_EXPR
// 等于表达式 == !=
%nterm <FE::AST::ExprNode*> EQUALITY_EXPR
// 逻辑与表达式 &&
%nterm <FE::AST::ExprNode*> LOGICAL_AND_EXPR
// 逻辑或表达式 ||
%nterm <FE::AST::ExprNode*> LOGICAL_OR_EXPR
// 赋值表达式=
%nterm <FE::AST::ExprNode*> ASSIGN_EXPR
// 非逗号表达式
%nterm <FE::AST::ExprNode*> NOCOMMA_EXPR
%nterm <FE::AST::ExprNode*> EXPR
// 表达式列表
%nterm <std::vector<FE::AST::ExprNode*>*> EXPR_LIST

// 数组维度表达式
%nterm <FE::AST::ExprNode*> ARRAY_DIMENSION_EXPR
// 数组维度表达式列表
%nterm <std::vector<FE::AST::ExprNode*>*> ARRAY_DIMENSION_EXPR_LIST
// 左值表达式
%nterm <FE::AST::ExprNode*> LEFT_VAL_EXPR

// 表达式语句
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
// 一般语句
%nterm <FE::AST::StmtNode*> STMT
// 语句列表
%nterm <std::vector<FE::AST::StmtNode*>*> STMT_LIST
// 程序根节点
%nterm <FE::AST::Root*> PROGRAM

//开始符号，program是整个语法分析的入口
%start PROGRAM

//THEN和ELSE用于处理if和else的移进-规约冲突
// if_stmt:
//      IF '(' expr ')' stmt %prec THEN
//    | IF '(' expr ')' stmt ELSE stmt %prec ELSE
//    ;
// else部分会被规约到最近的未匹配的if上
%precedence THEN
%precedence ELSE


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
        $$ = new Root($1);
        parser.ast = $$;
    }
    | PROGRAM END {
        YYACCEPT; // 特殊宏 表示立即结束解析并返回成功结果
    }
    ;

// -------- 语句列表 --------

// 由零个或多个语句组成的序列
// 使用左递归定义，避免右递归导致的栈溢出
STMT_LIST:
    STMT {
        $$ = new std::vector<StmtNode*>();  // 创建一个新的vector来存储语句序列
        if ($1) $$->push_back($1);  // 只有非空语句才加入列表（排除空语句和注释）
    }
    | STMT_LIST STMT {
        $$ = $1;
        if ($2) $$->push_back($2); // 追加新语句
    }
    ;

// -------- 语句分类 --------
STMT:
    EXPR_STMT {//1
        $$ = $1;    // 表达式语句: 以分号结尾的表达式
    }
    | VAR_DECL_STMT {
        $$ = $1;    // 变量声明语句: int a, b = 5;
    }
    | FUNC_DECL_STMT {
        $$ = $1;// 函数定义语句: int func(int a) { ... }
    }
    | FOR_STMT {
        $$ = $1;// for循环语句
    }
    | IF_STMT {
        $$ = $1;// if-else条件语句
    }
    | CONTINUE_STMT {
        $$ = $1;// continue
    }
    | SEMICOLON {
        $$ = nullptr; // 空语句: 单独的分号
    }
    | SLASH_COMMENT {
        $$ = nullptr; // 不用考虑
    }
    //TODO(Lab2)：考虑更多语句类型
    | BLOCK_STMT {
        $$ = $1;// 补充：代码块语句: { stmt1; stmt2; ... }
    }    
    | RETURN_STMT{
        $$ = $1;// 补充： return语句: return expr;
    }
    | WHILE_STMT{
        $$ = $1;// 补充： while循环语句: while (expr) stmt;
    }
    | BREAK_STMT{
        $$ = $1;// 补充： break语句: break;
    }
    ;



//接下来是详细的具体语句

// continue
CONTINUE_STMT:
    CONTINUE SEMICOLON {
        // 创建ContinueStmt节点 
        $$ = new ContinueStmt(@1.begin.line, @1.begin.column);
    }
    ;

// EXPR_STMT 任何表达式后跟分号的语句
EXPR_STMT:
    EXPR SEMICOLON {
        $$ = new ExprStmt($1, @1.begin.line, @1.begin.column);
    }
    ;

// 变量声明的核心规则 
VAR_DECLARATION:
    VAR_TYPE VAR_DECLARATOR_LIST {
        // false: 表示非const
        $$ = new VarDeclaration($1, $2, false, @1.begin.line, @1.begin.column);
    }
    | CONST VAR_TYPE VAR_DECLARATOR_LIST {
        // const常量声明
        // true: 表示const常量
        $$ = new VarDeclaration($2, $3, true, @1.begin.line, @1.begin.column);
    }
    ;

// 变量声明语句（含分号）
VAR_DECL_STMT:
    /* TODO(Lab2): Implement variable declaration statement rule */
    VAR_DECLARATION SEMICOLON {
        $$ = new VarDeclStmt($1, @1.begin.line, @1.begin.column);
    }
    ;

// FUNC_BODY: 函数的代码块部分
// 可以是 空语句{} 或  { stmt1; stmt2; ... }
FUNC_BODY:
    LBRACE RBRACE {
        $$ = nullptr;   // {} 空函数体
    }
    | LBRACE STMT_LIST RBRACE { // 非空函数体
        if (!$2 || $2->empty())
        {
            // 语句列表为空（可能只有注释或空语句）
            // 考虑 注释不被识别为token ，只能为空
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

// 函数定义语句
// 格式: 返回类型 函数名(参数列表) { 函数体 }
FUNC_DECL_STMT:
    TYPE IDENT LPAREN PARAM_DECLARATOR_LIST RPAREN FUNC_BODY {
        // $1: 返回类型 (Type*) - int/float/void
        // $2: 函数名 ( identifier string )
        // $4: 参数列表 (vector<ParamDeclarator*>*)
        // $6: 函数体 (StmtNode*)
        std::cerr<< "Defining function: " << $2 << std::endl;
        Entry* entry = Entry::getEntry($2);// 获取函数名对应的符号表项，可能已经存在
        // FuncDeclStmt是函数定义的AST节点
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
    IF LPAREN EXPR RPAREN STMT %prec THEN {
        // 无else分支的if语句
        // $3: 条件表达式
        // $5: then分支的语句
        $$ = new IfStmt($3, $5, nullptr, @1.begin.line, @1.begin.column);
    }
    | IF LPAREN EXPR RPAREN STMT ELSE STMT {
        // 有else分支的if语句
        // $7: else分支的语句
        $$ = new IfStmt($3, $5, $7, @1.begin.line, @1.begin.column);
    }
    ;

//TODO(Lab2)：按照你补充的语句类型，实现这些语句的处理

RETURN_STMT:
    RETURN EXPR SEMICOLON {
        // 有返回值的return语句: return expr;
        // $2: 返回值表达式 (ExprNode*)
        $$ = new ReturnStmt($2, @1.begin.line, @1.begin.column);
    }
    | RETURN SEMICOLON {
        // 无返回值的return语句: return;
        $$ = new ReturnStmt(nullptr, @1.begin.line, @1.begin.column);
    }
    ;
//以下为函数参数相关的规则

BREAK_STMT:
    BREAK SEMICOLON {
        $$ = new BreakStmt(@1.begin.line, @1.begin.column);
    }
    ;

WHILE_STMT:
    WHILE LPAREN EXPR RPAREN STMT {
        $$ = new WhileStmt($3, $5,@1.begin.line, @1.begin.column);
    }
    ;
// -------- 单个函数参数 --------
// PARAM_DECLARATOR: 函数形参声明
// 支持普通参数和数组参数
PARAM_DECLARATOR:
    VAR_TYPE IDENT {
        // 普通参数: int a,
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, nullptr, @1.begin.line, @1.begin.column);
    }
    | VAR_TYPE IDENT LBRACKET RBRACKET {
        // 一维数组参数: int arr[], float data[]
        // 数组作为参数时，第一维的大小可以省略       
        std::vector<ExprNode*>* dim = new std::vector<ExprNode*>();
        dim->emplace_back(new LiteralExpr(-1, @3.begin.line, @3.begin.column));
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, dim, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2)：考虑函数形参更多情况
    | VAR_TYPE IDENT LBRACKET RBRACKET ARRAY_DIMENSION_EXPR_LIST  {//1
        // 多维数组参数: int arr[][10], float data[][20][30]
        // 第一维可省略，后续维度必须指定
        std::vector<ExprNode*>* dim = new std::vector<ExprNode*>();
        dim->emplace_back(new LiteralExpr(-1, @3.begin.line, @3.begin.column)); // 第一维省略
        // $5 是 std::vector<ExprNode*>*，将其元素追加到 dim
        dim->insert(dim->end(), $5->begin(), $5->end()); // 后续维度
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, dim, @1.begin.line, @1.begin.column);
    }
    | VAR_TYPE IDENT ARRAY_DIMENSION_EXPR_LIST {
        // 多维数组参数: int arr[10][20], float data[30][40][50]
        std::vector<ExprNode*>* dim = $3; // 获取所有维度表达式
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, dim, @1.begin.line, @1.begin.column);
    }
    ;

// 函数的所有形参，参数列表
// 可以是空列表（无参函数）或多个参数（用逗号分隔）
PARAM_DECLARATOR_LIST:
    /* empty */ {
        // 无参数: void func()
        // 创建空的参数列表
        $$ = new std::vector<ParamDeclarator*>();
    }
    //TODO(Lab2)：考虑函数形参列表的构成情况
    | PARAM_DECLARATOR {// 1
        // 单个参数: int a
        $$ = new std::vector<ParamDeclarator*>();
        $$->push_back($1);
    }
    | PARAM_DECLARATOR_LIST COMMA PARAM_DECLARATOR {
        // 多个参数: int a, float b, int arr[]
        $$ = $1;
        $$->push_back($3);
    }
    ;


//下面是变量声明相关规则


// 单个变量的声明
// 包含变量名、数组维度（如果是数组）、初始值（如果有初始化）
VAR_DECLARATOR:
    //TODO(Lab2)：完成变量声明符的处理
    IDENT {
            // 1. 简单变量: int a;
            Entry* entry = Entry::getEntry($1);
            ExprNode* lval = new LeftValExpr(entry, nullptr);
            $$ = new VarDeclarator(lval, nullptr);
        }
    | IDENT ASSIGN INITIALIZER {
            // 2. 带初始化: int a = 5;
            Entry* entry = Entry::getEntry($1);
            ExprNode* lval = new LeftValExpr(entry, nullptr);
            $$ = new VarDeclarator(lval, $3);
        } 
    | IDENT ARRAY_DIMENSION_EXPR_LIST {
            // 3. 数组: int arr[10];
            Entry* entry = Entry::getEntry($1);
            ExprNode* lval = new LeftValExpr(entry, $2);
            $$ = new VarDeclarator(lval, nullptr);
        }
    | IDENT ARRAY_DIMENSION_EXPR_LIST ASSIGN INITIALIZER {
            // 数组带初始化: int arr[10] = {1, 2, 3};
            Entry* entry = Entry::getEntry($1);
            ExprNode* lval = new LeftValExpr(entry, $2);
            $$ = new VarDeclarator(lval, $4);
        }
    | IDENT LBRACKET RBRACKET ARRAY_DIMENSION_EXPR_LIST {
            // 允许首维省略的多维数组声明: int arr[][n];
            // 用 -1 作为占位，后续在语义阶段检查合法性（需配合常量表达式判断或初始化推断）
            Entry* entry = Entry::getEntry($1);
            std::vector<ExprNode*>* dims = new std::vector<ExprNode*>();
            dims->emplace_back(new LiteralExpr(-1, @2.begin.line, @2.begin.column));
            // 追加后续维度
            dims->insert(dims->end(), $4->begin(), $4->end());
            ExprNode* lval = new LeftValExpr(entry, dims);
            $$ = new VarDeclarator(lval, nullptr);
        }
    | IDENT LBRACKET RBRACKET ARRAY_DIMENSION_EXPR_LIST ASSIGN INITIALIZER {
            // 允许首维省略并带初始化: int arr[][n] = {...};
            Entry* entry = Entry::getEntry($1);
            std::vector<ExprNode*>* dims = new std::vector<ExprNode*>();
            dims->emplace_back(new LiteralExpr(-1, @2.begin.line, @2.begin.column));
            dims->insert(dims->end(), $4->begin(), $4->end());
            ExprNode* lval = new LeftValExpr(entry, dims);
            $$ = new VarDeclarator(lval, $6);
        }
    | IDENT LBRACKET RBRACKET ASSIGN INITIALIZER {
            // 允许一维数组首维省略并带初始化: int a[] = {1,2,3};
            Entry* entry = Entry::getEntry($1);
            std::vector<ExprNode*>* dims = new std::vector<ExprNode*>();
            dims->emplace_back(new LiteralExpr(-1, @2.begin.line, @2.begin.column));
            ExprNode* lval = new LeftValExpr(entry, dims);
            $$ = new VarDeclarator(lval, $5);
        }
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


// 变量的初始值
// 可以是单个表达式或初始化列表
INITIALIZER:
    /* TODO(Lab2): Implement variable initializer rule */
    NOCOMMA_EXPR {
        // 1. 单个表达式初始化: int a = 5 + 3;
        $$ = new Initializer($1, @1.begin.line, @1.begin.column);
    }
    |
    LBRACE INITIALIZER_LIST RBRACE {
        // 2. 初始化列表: int arr[] = {1, 2, 3};
        $$ = new InitializerList($2, @1.begin.line, @1.begin.column);
    }
    |
    LBRACE RBRACE {
        // 3. 空初始化列表: int arr[] = {};
        auto empty = new std::vector<InitDecl*>();
        $$ = new InitializerList(empty, @1.begin.line, @1.begin.column);
    }
    ;


// 用于数组的初始化列表，其实和前面的多参数很像，都是第一个加追加。
// 格式: init 或者 {init1, init2, init3, ...}
INITIALIZER_LIST:
    INITIALIZER {
        // 第一个初始化项
        $$ = new std::vector<InitDecl*>();
        $$->push_back($1);
    }
    | INITIALIZER_LIST COMMA INITIALIZER {
        // 追加更多初始化项
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
// a = b + c 
ASSIGN_EXPR:
    // TODO(Lab2): 完成赋值表达式的处理
    LEFT_VAL_EXPR ASSIGN NOCOMMA_EXPR {
        // 1. 简单赋值: a = 5
        $$ = new BinaryExpr(FE::AST::Operator::ASSIGN, $1, $3, @2.begin.line, @2.begin.column);
    }
    | LEFT_VAL_EXPR PLUS_ASSIGN NOCOMMA_EXPR {
        // 复合赋值: a += 5 
        $$ = new BinaryExpr(FE::AST::Operator::ASSIGN, $1, $3, @2.begin.line, @2.begin.column);
    }
    | LEFT_VAL_EXPR MINUS_ASSIGN NOCOMMA_EXPR {
        // 复合赋值: a -= 5
        $$ = new BinaryExpr(FE::AST::Operator::ASSIGN, $1, $3, @2.begin.line, @2.begin.column);
    }
    | LEFT_VAL_EXPR MUL_ASSIGN NOCOMMA_EXPR {
        // 复合赋值: a *= 5
        $$ = new BinaryExpr(FE::AST::Operator::ASSIGN, $1, $3, @2.begin.line, @2.begin.column);
    }
    | LEFT_VAL_EXPR DIV_ASSIGN NOCOMMA_EXPR {
        // 复合赋值: a /= 5
        $$ = new BinaryExpr(FE::AST::Operator::ASSIGN, $1, $3, @2.begin.line, @2.begin.column);
    }
    | LEFT_VAL_EXPR MOD_ASSIGN NOCOMMA_EXPR {
        // 复合赋值: a %= 5
        $$ = new BinaryExpr(FE::AST::Operator::ASSIGN, $1, $3, @2.begin.line, @2.begin.column);
    }
    ;



// 用于函数调用的参数列表，列表类都很像，第一个和追加
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

// 除逗号运算符外的所有表达式
// 用于不允许逗号运算符的地方: 函数参数、赋值右值等
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
    LOGICAL_AND_EXPR {
        // 单个逻辑与表达式，无逻辑或运算
        $$ = $1;
    }
    | LOGICAL_OR_EXPR OR LOGICAL_AND_EXPR {
        // 逻辑或运算: expr1 || expr2
        $$ = new BinaryExpr(FE::AST::Operator::OR, $1, $3, @2.begin.line, @2.begin.column);
    }
    ;

// 运算符: &&
// 优先级: 低于相等运算, 高于逻辑或
// 短路求值: 左操作数为false时，不计算右操作数
LOGICAL_AND_EXPR:
    /* TODO(Lab2): Implement logical AND expression rule */
    EQUALITY_EXPR {
        // 单个相等表达式，无逻辑与运算
        $$ = $1;
    }
    | LOGICAL_AND_EXPR AND  EQUALITY_EXPR{
        // 逻辑与运算: expr1 && expr2
        $$ = new BinaryExpr(FE::AST::Operator::AND, $1, $3, @2.begin.line, @2.begin.column);
    }
    ;

// 运算符: == !=
// 优先级: 低于关系运算, 高于逻辑与
// 返回: 布尔值（0或1）
EQUALITY_EXPR:
    /* TODO(Lab2): Implement equality expression rule */
    RELATIONAL_EXPR {
        // 单个关系表达式，无相等运算
        $$ = $1;
    }
    | EQUALITY_EXPR EQ RELATIONAL_EXPR {
        // 相等运算: expr1 == expr2
        $$ = new BinaryExpr(FE::AST::Operator::EQ, $1, $3, @2.begin.line, @2.begin.column);
    }
    | EQUALITY_EXPR NE RELATIONAL_EXPR {
        // 不等运算: expr1 != expr2
        $$ = new BinaryExpr(FE::AST::Operator::NEQ, $1, $3, @2.begin.line, @2.begin.column);
    }
    ;


// 运算符: < > <= >=
// 优先级: 低于加减, 高于相等
// 返回: 布尔值（0或1）
RELATIONAL_EXPR:
    /* TODO(Lab2): Implement relational expression rule */
    ADDSUB_EXPR {
        // 单个加减表达式，无关系运算
        $$ = $1;
    }
    | RELATIONAL_EXPR LT ADDSUB_EXPR {
        // 小于运算: expr1 < expr2
        $$ = new BinaryExpr(FE::AST::Operator::LT, $1, $3, @2.begin.line, @2.begin.column);
    }
    | RELATIONAL_EXPR GT ADDSUB_EXPR {
        // 大于运算: expr1 > expr2
        $$ = new BinaryExpr(FE::AST::Operator::GT, $1, $3, @2.begin.line, @2.begin.column);
    }
    | RELATIONAL_EXPR LE ADDSUB_EXPR {
        // 小于等于运算: expr1 <= expr2 
        $$ = new BinaryExpr(FE::AST::Operator::LE, $1, $3, @2.begin.line, @2.begin.column);
    }
    | RELATIONAL_EXPR GE ADDSUB_EXPR {
        // 大于等于运算: expr1 >= expr2
        $$ = new BinaryExpr(FE::AST::Operator::GE, $1, $3, @2.begin.line, @2.begin.column);
    }
    ;

// 运算符: + -
// 优先级: 低于乘除, 高于关系
// 左结合: a - b + c = (a - b) + c
ADDSUB_EXPR:
    /* TODO(Lab2): Implement addition and subtraction expression rule */
    MULDIV_EXPR {
        // 单个乘除表达式，无加减运算
        $$ = $1;
    }
    | ADDSUB_EXPR PLUS MULDIV_EXPR {
        // 加法运算: expr1 + expr2
        $$ = new BinaryExpr(FE::AST::Operator::ADD, $1, $3, @2.begin.line, @2.begin.column);
    }   
    | ADDSUB_EXPR MINUS MULDIV_EXPR {
        // 减法运算: expr1 - expr2
        $$ = new BinaryExpr(FE::AST::Operator::SUB, $1, $3, @2.begin.line, @2.begin.column);
    }
    ;

// 运算符: * / %
// 优先级: 低于一元, 高于加减
// 左结合: a / b * c = (a / b) * c
MULDIV_EXPR:
    /* TODO(Lab2): Implement multiplication and division expression rule */
    UNARY_EXPR {
        // 单个一元表达式，无乘除运算
        $$ = $1;
    }
    | MULDIV_EXPR STAR UNARY_EXPR {
        // 乘法运算: expr1 * expr2
        $$ = new BinaryExpr(FE::AST::Operator::MUL, $1, $3, @2.begin.line, @2.begin.column);
    }
    | MULDIV_EXPR SLASH UNARY_EXPR { 
        // 除法运算: expr1 / expr2
        $$ = new BinaryExpr(FE::AST::Operator::DIV, $1, $3, @2.begin.line, @2.begin.column);
    }
    | MULDIV_EXPR MOD UNARY_EXPR {
        // 取模运算: expr1 % expr2
        $$ = new BinaryExpr(FE::AST::Operator::MOD, $1, $3, @2.begin.line, @2.begin.column);
    }
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
        // 常量表达式
        $$ = $1;
    }
    | LEFT_VAL_EXPR {
        // 变量或数组元素
        $$ = $1;
    }
    | LPAREN EXPR RPAREN {
        // 括号表达式: (expr)
        $$ = $2;
    }
    | FUNC_CALL_EXPR {
        // 函数调用表达式
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

// 数组维度表达式列表
// 格式: [expr1][expr2][expr3]...
ARRAY_DIMENSION_EXPR_LIST:
    /* TODO(Lab2): Implement variable dimension rule */
    ARRAY_DIMENSION_EXPR {
        // 第一个维度
        $$ = new std::vector<ExprNode*>();
        $$->push_back($1);
    }
    | ARRAY_DIMENSION_EXPR_LIST ARRAY_DIMENSION_EXPR {
        // 追加更多维度
        $$ = $1;
        $$->push_back($2);
    }
    ;


// 可以出现在赋值号左边的表达式
// 包括: 变量、数组元素
LEFT_VAL_EXPR:
    IDENT {
        // 简单变量
        Entry* entry = Entry::getEntry($1);
        $$ = new LeftValExpr(entry, nullptr, @1.begin.line, @1.begin.column);
    }
    | IDENT ARRAY_DIMENSION_EXPR_LIST {
        // 数组元素
        Entry* entry = Entry::getEntry($1);
        $$ = new LeftValExpr(entry, $2, @1.begin.line, @1.begin.column);
    }
    ;

// 常量值
// 包括: 整数、长整数、浮点数
LITERAL_EXPR:
    INT_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2): 处理更多字面量
    | FLOAT_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    } 
    | LL_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    }
    | STR_CONST {
        $$ = new LiteralExpr(0, @1.begin.line, @1.begin.column);
    }
    ;

// TYPE: 基本数据类型
// SysY支持: int, float, void
TYPE:
    // TODO(Lab2): 完成类型的处理
    INT {
        $$ = FE::AST::TypeFactory::getBasicType(FE::AST::Type_t::INT);
    }
    | FLOAT {
        $$ = FE::AST::TypeFactory::getBasicType(FE::AST::Type_t::FLOAT);
    }
    | VOID {
        $$ = FE::AST::TypeFactory::getBasicType(FE::AST::Type_t::VOID);
    }
    ;

// VAR_TYPE: 仅用于变量声明的类型，禁止 void
VAR_TYPE:
    INT {
        $$ = FE::AST::TypeFactory::getBasicType(FE::AST::Type_t::INT);
    }
    | FLOAT {
        $$ = FE::AST::TypeFactory::getBasicType(FE::AST::Type_t::FLOAT);
    }
    ;

// UNARY_OP: 一元运算符到Operator枚举的映射
UNARY_OP:
    // TODO(Lab2): 完成一元运算符的处理
    PLUS {
        $$ = FE::AST::Operator::ADD;
    }
    | MINUS {
        $$ = FE::AST::Operator::SUB;
    }
    | NOT {
        $$ = FE::AST::Operator::NOT;
    }
    ;
// 补充：代码块语句: { stmt1; stmt2; ... }
BLOCK_STMT:
    LBRACE STMT_LIST RBRACE {
        // 创建一个 BlockStmt，包含语句列表
        $$ = new BlockStmt($2, @1.begin.line, @1.begin.column);
    }
    |
    LBRACE RBRACE {
        // 空代码块 {}
        $$ = new BlockStmt(new std::vector<StmtNode*>(), @1.begin.line, @1.begin.column);
    }
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
