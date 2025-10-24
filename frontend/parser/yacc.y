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

// 定义 语法分析的起始符号
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

// 语法树匹配从这里开始
// 语法规则
// 
PROGRAM:
    STMT_LIST {
        $$ = new Root($1);
        parser.ast = $$;
    }
    | PROGRAM END {
        YYACCEPT;
    }
    ;

STMT_LIST:
    STMT {
        $$ = new std::vector<StmtNode*>();
        if ($1) $$->push_back($1);
    }
    | STMT_LIST STMT {
        $$ = $1;
        if ($2) $$->push_back($2);
    }
    ;

STMT:
    EXPR_STMT {
        $$ = $1;
    }
    | VAR_DECL_STMT {
        $$ = $1;
    }
    | FUNC_DECL_STMT {
        $$ = $1;
    }
    | FOR_STMT {
        $$ = $1;
    }
    | IF_STMT {
        $$ = $1;
    }
    | CONTINUE_STMT {
        $$ = $1;
    }
    | SEMICOLON {
        $$ = nullptr;
    }
    | SLASH_COMMENT {
        $$ = nullptr;
    }
    //TODO(Lab2)：考虑更多语句类型
    ;

CONTINUE_STMT:
    CONTINUE SEMICOLON {
        $$ = new ContinueStmt(@1.begin.line, @1.begin.column);
    }
    ;

EXPR_STMT:
    EXPR SEMICOLON {
        $$ = new ExprStmt($1, @1.begin.line, @1.begin.column);
    }
    ;

VAR_DECLARATION:
    TYPE VAR_DECLARATOR_LIST {
        $$ = new VarDeclaration($1, $2, false, @1.begin.line, @1.begin.column);
    }
    | CONST TYPE VAR_DECLARATOR_LIST {
        $$ = new VarDeclaration($2, $3, true, @1.begin.line, @1.begin.column);
    }
    ;

VAR_DECL_STMT:
    /* TODO(Lab2): Implement variable declaration statement rule */
    ;

FUNC_BODY:
    LBRACE RBRACE {
        $$ = nullptr;
    }
    | LBRACE STMT_LIST RBRACE {
        if (!$2 || $2->empty())
        {
            $$ = nullptr;
            delete $2;
        }
        else if ($2->size() == 1)
        {
            $$ = (*$2)[0];
            delete $2;
        }
        else $$ = new BlockStmt($2, @1.begin.line, @1.begin.column);
    }
    ;

FUNC_DECL_STMT:
    TYPE IDENT LPAREN PARAM_DECLARATOR_LIST RPAREN FUNC_BODY {
        Entry* entry = Entry::getEntry($2);
        $$ = new FuncDeclStmt($1, entry, $4, $6, @1.begin.line, @1.begin.column);
    }
    ;

FOR_STMT:
    FOR LPAREN VAR_DECLARATION SEMICOLON EXPR SEMICOLON EXPR RPAREN STMT {
        VarDeclStmt* initStmt = new VarDeclStmt($3, @3.begin.line, @3.begin.column);
        $$ = new ForStmt(initStmt, $5, $7, $9, @1.begin.line, @1.begin.column);
    }
    | FOR LPAREN EXPR SEMICOLON EXPR SEMICOLON EXPR RPAREN STMT {
        StmtNode* initStmt = new ExprStmt($3, $3->line_num, $3->col_num);
        $$ = new ForStmt(initStmt, $5, $7, $9, @1.begin.line, @1.begin.column);
    }
    ;

IF_STMT:
    /* TODO(Lab2): Implement if statement rule */
    ;

//TODO(Lab2)：按照你补充的语句类型，实现这些语句的处理


PARAM_DECLARATOR:
    TYPE IDENT {
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, nullptr, @1.begin.line, @1.begin.column);
    }
    | TYPE IDENT LBRACKET RBRACKET {
        std::vector<ExprNode*>* dim = new std::vector<ExprNode*>();
        dim->emplace_back(new LiteralExpr(-1, @3.begin.line, @3.begin.column));
        Entry* entry = Entry::getEntry($2);
        $$ = new ParamDeclarator($1, entry, dim, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2)：考虑函数形参更多情况
    ;

PARAM_DECLARATOR_LIST:
    /* empty */ {
        $$ = new std::vector<ParamDeclarator*>();
    }
    //TODO(Lab2)：考虑函数形参列表的构成情况
    ;

VAR_DECLARATOR:
    //TODO(Lab2)：完成变量声明符的处理
    ;

VAR_DECLARATOR_LIST:
    VAR_DECLARATOR {
        $$ = new std::vector<VarDeclarator*>();
        $$->push_back($1);
    }
    | VAR_DECLARATOR_LIST COMMA VAR_DECLARATOR {
        $$ = $1;
        $$->push_back($3);
    }
    ;

INITIALIZER:
    /* TODO(Lab2): Implement variable initializer rule */
    ;

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

ASSIGN_EXPR:
    // TODO(Lab2): 完成赋值表达式的处理
    ;

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

EXPR:
    NOCOMMA_EXPR {
        $$ = $1;
    }
    | EXPR COMMA NOCOMMA_EXPR {
        if ($1->isCommaExpr()) {
            CommaExpr* ce = static_cast<CommaExpr*>($1);
            ce->exprs->push_back($3);
            $$ = ce;
        } else {
            auto vec = new std::vector<ExprNode*>();
            vec->push_back($1);
            vec->push_back($3);
            $$ = new CommaExpr(vec, $1->line_num, $1->col_num);
        }
    }
    ;

NOCOMMA_EXPR:
    LOGICAL_OR_EXPR {
        $$ = $1;
    }
    | ASSIGN_EXPR {
        $$ = $1;
    }
    ;

LOGICAL_OR_EXPR:
    /* TODO(Lab2): Implement logical OR expression rule */
    ;

LOGICAL_AND_EXPR:
    /* TODO(Lab2): Implement logical AND expression rule */
    ;

EQUALITY_EXPR:
    /* TODO(Lab2): Implement equality expression rule */
    ;

RELATIONAL_EXPR:
    /* TODO(Lab2): Implement relational expression rule */
    ;

ADDSUB_EXPR:
    /* TODO(Lab2): Implement addition and subtraction expression rule */
    ;

MULDIV_EXPR:
    /* TODO(Lab2): Implement multiplication and division expression rule */
    ;

UNARY_EXPR:
    BASIC_EXPR {
        $$ = $1;
    }
    | UNARY_OP UNARY_EXPR {
        $$ = new UnaryExpr($1, $2, $2->line_num, $2->col_num);
    }
    ;

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

ARRAY_DIMENSION_EXPR:
    LBRACKET NOCOMMA_EXPR RBRACKET {
        $$ = $2;
    }
    ;

ARRAY_DIMENSION_EXPR_LIST:
    /* TODO(Lab2): Implement variable dimension rule */
    ;

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

LITERAL_EXPR:
    INT_CONST {
        $$ = new LiteralExpr($1, @1.begin.line, @1.begin.column);
    }
    //TODO(Lab2): 处理更多字面量
    ;

TYPE:
    // TODO(Lab2): 完成类型的处理
    ;

UNARY_OP:
    // TODO(Lab2): 完成一元运算符的处理
    ;

%%

void FE::YaccParser::error(const FE::location& location, const std::string& message)
{
    std::cerr << "msg: " << message << ", error happened at: " << location << std::endl;
}
