#ifndef __FRONTEND_AST_DECL_H__
#define __FRONTEND_AST_DECL_H__

#include <frontend/ast/ast.h>
/*
本文件是AST的声明类节点类定义集合。
如果需要，你可以在类中添加成员变量和成员函数，辅助你完成实验
*/
namespace FE::AST
{
    // 声明类节点的基类
    class DeclNode : public Node
    {
      public:
        DeclNode(int line_num = -1, int col_num = -1) : Node(line_num, col_num) {}
        virtual ~DeclNode() override = default;

        virtual void accept(Visitor& visitor) override = 0;
    };

    // 带初始化的声明节点
    class InitDecl : public DeclNode
    {
      public:
        bool singleInit;  // 是否是单个初始化表达式

        InitDecl(bool singleInit = false, int line_num = -1, int col_num = -1)
            : DeclNode(line_num, col_num), singleInit(singleInit)
        {}
        virtual ~InitDecl() override = default;

        virtual void accept(Visitor& visitor) override = 0;  // 纯虚函数
    };

    // 单个初始化表达式，如 int a = 5 + b 的 5 + b
    class Initializer : public InitDecl
    {
      public:
        ExprNode* init_val;  // 初始化表达式

      public:
        Initializer(ExprNode* expr, int line_num = -1, int col_num = -1)
            : InitDecl(true, line_num, col_num), init_val(expr)
        {}
        virtual ~Initializer() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

    // 初始化列表，如 int arr[3] = {1, 2, 3} 的 {1, 2, 3}
    // 如果你不打算实现数组的支持，可以不实现这个类
    class InitializerList : public InitDecl
    {
      public:
        std::vector<InitDecl*>* init_list;  // 初始化列表

      public:
        InitializerList(std::vector<InitDecl*>* init_list, int line_num = -1, int col_num = -1)
            : InitDecl(false, line_num, col_num), init_list(init_list)
        {}
        virtual ~InitializerList() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }

        size_t size();
    };

    // 单个变量声明节点，如 int a = 5; 中的 a
    class VarDeclarator : public DeclNode
    {
      public:
        ExprNode* lval;  // 变量左值表达式
        InitDecl* init;  // 变量初始化部分，可以为空

      public:
        VarDeclarator(ExprNode* lval, InitDecl* init = nullptr, int line_num = -1, int col_num = -1)
            : DeclNode(line_num, col_num), lval(lval), init(init)
        {}
        virtual ~VarDeclarator() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

    // 函数参数声明节点，如 int a, float b[]
    class ParamDeclarator : public DeclNode
    {
      public:
        Type*                   type;   // 参数类型
        Entry*                  entry;  // 符号表项
        std::vector<ExprNode*>* dims;   // 参数维度列表，可以为空

      public:
        ParamDeclarator(
            Type* type, Entry* entry, std::vector<ExprNode*>* dims = nullptr, int line_num = -1, int col_num = -1)
            : DeclNode(line_num, col_num), type(type), entry(entry), dims(dims)
        {}
        virtual ~ParamDeclarator() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

    // 变量声明语句节点，如 int a = 5, arr[10] = {1,2,3};
    // 它们的具体声明都放在 decls 里
    // isConstDecl 标记是否是 const 声明
    class VarDeclaration : public DeclNode
    {
      public:
        Type*                        type;         // 变量类型
        std::vector<VarDeclarator*>* decls;        // 变量声明列表
        bool                         isConstDecl;  // 是否是 const 声明

      public:
        VarDeclaration(Type* type, std::vector<VarDeclarator*>* decls, bool isConstDecl = false, int line_num = -1,
            int col_num = -1)
            : DeclNode(line_num, col_num), type(type), decls(decls), isConstDecl(isConstDecl)
        {}
        virtual ~VarDeclaration() override;

        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };

}  // namespace FE::AST

#endif  // __FRONTEND_AST_DECL_H__
