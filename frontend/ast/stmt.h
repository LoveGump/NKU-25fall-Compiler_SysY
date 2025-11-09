#ifndef __FRONTEND_AST_STMT_H__
#define __FRONTEND_AST_STMT_H__

#include <frontend/ast/ast.h>
#include <frontend/ast/decl.h>
/*
本文件是AST的语句类节点类定义集合。
如果需要，你可以在类中添加成员变量和成员函数，辅助你完成实验
*/
namespace FE::AST
{   
    // 语句节点基类
    class StmtNode : public Node
    {
      public:
        StmtNode(int line_num = -1, int col_num = -1) : Node(line_num, col_num) {}
        virtual ~StmtNode() override = default;

        virtual void accept(Visitor& visitor) override = 0;
        virtual bool isVarDeclStmt()                   = 0;
    };

    // 表达式语句，如 a = b + 3;
    class ExprStmt : public StmtNode
    {
      public:
        ExprNode* expr;

      public:
        ExprStmt(ExprNode* expr, int line_num = -1, int col_num = -1) : StmtNode(line_num, col_num), expr(expr) {}
        virtual ~ExprStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    class ParamDeclarator;

    // 函数声明语句，如 int func(int a, float b) { ... }
    class FuncDeclStmt : public StmtNode
    {
      public:
        Type*                          retType;
        Entry*                         entry;
        std::vector<ParamDeclarator*>* params;
        StmtNode*                      body;

      public:
        FuncDeclStmt(Type* retType, Entry* entry, std::vector<ParamDeclarator*>* params, StmtNode* body = nullptr,
            int line_num = -1, int col_num = -1)
            : StmtNode(line_num, col_num), retType(retType), entry(entry), params(params), body(body)
        {}
        virtual ~FuncDeclStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    class VarDeclarator;
    // 变量声明语句节点，如 int a = 5, arr[10] = {1,2,3};
    class VarDeclStmt : public StmtNode
    {
      public:
        VarDeclaration* decl;

      public:
        VarDeclStmt(VarDeclaration* decl, int line_num = -1, int col_num = -1) : StmtNode(line_num, col_num), decl(decl)
        {}
        virtual ~VarDeclStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return true; }
    };

    // 复合语句，如 { ... }，其内部可以包含多条语句
    class BlockStmt : public StmtNode
    {
      public:
        std::vector<StmtNode*>* stmts;

      public:
        BlockStmt(std::vector<StmtNode*>* stmts, int line_num = -1, int col_num = -1)
            : StmtNode(line_num, col_num), stmts(stmts)
        {}
        virtual ~BlockStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    // 返回语句，如 return a + 1;
    class ReturnStmt : public StmtNode
    {
      public:
        ExprNode* retExpr;

      public:
        ReturnStmt(ExprNode* retExpr = nullptr, int line_num = -1, int col_num = -1)
            : StmtNode(line_num, col_num), retExpr(retExpr)
        {}
        virtual ~ReturnStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    // while 语句，如 while (cond) { ... }
    // 其中的 cond 是条件表达式，body 是循环体
    class WhileStmt : public StmtNode
    {
      public:
        ExprNode* cond; // 条件表达式
        StmtNode* body; // 循环体

      public:
        WhileStmt(ExprNode* cond = nullptr, StmtNode* body = nullptr, int line_num = -1, int col_num = -1)
            : StmtNode(line_num, col_num), cond(cond), body(body)
        {}
        virtual ~WhileStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    // if 语句，如 if (cond) { ... } else { ... }
    // 其中的 cond 是条件表达式，thenStmt 是条件为真时执行的语句，elseStmt 是条件为假时执行的语句（可选）
    class IfStmt : public StmtNode
    {
      public:
        ExprNode* cond; // 条件表达式
        StmtNode* thenStmt; // 条件为真时执行的语句
        StmtNode* elseStmt; // 条件为假时执行的语句（可选）

      public:
        IfStmt(ExprNode* cond = nullptr, StmtNode* thenStmt = nullptr, StmtNode* elseStmt = nullptr, int line_num = -1,
            int col_num = -1)
            : StmtNode(line_num, col_num), cond(cond), thenStmt(thenStmt), elseStmt(elseStmt)
        {}
        virtual ~IfStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    class BreakStmt : public StmtNode
    {
      public:
        BreakStmt(int line_num = -1, int col_num = -1) : StmtNode(line_num, col_num) {}
        virtual ~BreakStmt() override = default;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    class ContinueStmt : public StmtNode
    {
      public:
        ContinueStmt(int line_num = -1, int col_num = -1) : StmtNode(line_num, col_num) {}
        virtual ~ContinueStmt() override = default;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

    // for 语句，如 for (init; cond; step) { ... }
    // 其中的 init 是初始化语句，cond 是循环条件表达式，step 是每次循环后的步进语句，body 是循环体
    // 这一 statement 并非 SysY 语言标准的一部分，你可以不实现它
    class ForStmt : public StmtNode
    {
      public:
        StmtNode* init; // 初始化语句
        ExprNode* cond; // 循环条件表达式
        ExprNode* step; // 步进语句
        StmtNode* body; // 循环体

      public:
        ForStmt(StmtNode* init = nullptr, ExprNode* cond = nullptr, ExprNode* step = nullptr, StmtNode* body = nullptr,
            int line_num = -1, int col_num = -1)
            : StmtNode(line_num, col_num), init(init), cond(cond), step(step), body(body)
        {}
        virtual ~ForStmt() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
        virtual bool isVarDeclStmt() override { return false; }
    };

}  // namespace FE::AST

#endif  // __FRONTEND_AST_STMT_H__
