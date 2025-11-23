#ifndef __FRONTEND_AST_EXPR_H__
#define __FRONTEND_AST_EXPR_H__

#include <frontend/ast/ast.h>
/*
本文件是AST的表达式类节点类定义集合。
如果需要，你可以在类中添加成员变量和成员函数，辅助你完成实验
*/
namespace FE::AST
{
    // 表达式节点基类
    class ExprNode : public Node
    {
      public:
        size_t trueTar;   // 真的目标地址
        size_t falseTar;  // 假的目标地址

      public:
        ExprNode(int line_num = -1, int col_num = -1)
            : Node(line_num, col_num), trueTar(static_cast<size_t>(-1)), falseTar(static_cast<size_t>(-1))
        {}
        virtual ~ExprNode() override = default;

        virtual void accept(Visitor& visitor) override = 0;

        // 是否为逗号表达式
        virtual bool isCommaExpr() const { return false; }
        // 是否为字面量表达式
        virtual bool isLiteralExpr() const { return false; }
    };

    // 左值表达式，这里粗略指代允许出现在赋值号左边的表达式
    // 其中的 isLval 标记该表达式是否真的是左值
    class LeftValExpr : public ExprNode
    {
      public:
        bool                    isLval;
        Entry*                  entry;    // 符号表项
        std::vector<ExprNode*>* indices;  // 数组下标表达式列表

      public:
        LeftValExpr(Entry* entry, std::vector<ExprNode*>* indices = nullptr, int line_num = -1, int col_num = -1)
            : ExprNode(line_num, col_num), entry(entry), indices(indices)
        {}
        virtual ~LeftValExpr() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

    // 字面量表达式，如整数、浮点数等。其具体数值存储在 literal 中
    class LiteralExpr : public ExprNode
    {
      public:
        VarValue literal;

      public:
        LiteralExpr(int v, int line_num = -1, int col_num = -1) : ExprNode(line_num, col_num), literal(v) {}
        LiteralExpr(long long v, int line_num = -1, int col_num = -1) : ExprNode(line_num, col_num), literal(v) {}
        LiteralExpr(float v, int line_num = -1, int col_num = -1) : ExprNode(line_num, col_num), literal(v) {}
        virtual ~LiteralExpr() override = default;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }

        virtual bool isLiteralExpr() const override { return true; }
    };

    // 一元表达式，如 -a、!b 等
    class UnaryExpr : public ExprNode
    {
      public:
        Operator  op;
        ExprNode* expr;  // 操作数表达式

      public:
        UnaryExpr(Operator op, ExprNode* expr, int line_num = -1, int col_num = -1)
            : ExprNode(line_num, col_num), op(op), expr(expr)
        {}
        virtual ~UnaryExpr() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

    // 二元表达式，如 a + b、x * y 等
    class BinaryExpr : public ExprNode
    {
      public:
        Operator  op;
        ExprNode* lhs;
        ExprNode* rhs;

      public:
        BinaryExpr(Operator op, ExprNode* lhs, ExprNode* rhs, int line_num = -1, int col_num = -1)
            : ExprNode(line_num, col_num), op(op), lhs(lhs), rhs(rhs)
        {}
        virtual ~BinaryExpr() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

    // 函数调用表达式，需记录函数的符号表项以及调用时的实参列表
    class CallExpr : public ExprNode
    {
      public:
        Entry*                  func;
        std::vector<ExprNode*>* args;

      public:
        CallExpr(Entry* func, std::vector<ExprNode*>* args = nullptr, int line_num = -1, int col_num = -1)
            : ExprNode(line_num, col_num), func(func), args(args)
        {}
        virtual ~CallExpr() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

    // 逗号表达式，如 a = (b = 3, b + 2) 中的 b = 3, b + 2
    // 按 C 语言标准，逗号表达式的值为最后一个子表达式的值，上式中即 b + 2
    class CommaExpr : public ExprNode
    {
      public:
        std::vector<ExprNode*>* exprs;

      public:
        CommaExpr(std::vector<ExprNode*>* exprs, int line_num = -1, int col_num = -1)
            : ExprNode(line_num, col_num), exprs(exprs)
        {}
        virtual ~CommaExpr() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }

        virtual bool isCommaExpr() const override { return true; }
    };
}  // namespace FE::AST

#endif  // __FRONTEND_AST_EXPR_H__
