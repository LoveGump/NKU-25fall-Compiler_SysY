#ifndef __FRONTEND_AST_VISITOR_SEMENTIC_CHECK_AST_CHECKER_H__
#define __FRONTEND_AST_VISITOR_SEMENTIC_CHECK_AST_CHECKER_H__

#include <frontend/ast/ast_visitor.h>
#include <frontend/ast/ast.h>
#include <frontend/ast/decl.h>
#include <frontend/ast/expr.h>
#include <frontend/ast/stmt.h>
#include <frontend/symbol/symbol_table.h>
#include <vector>

namespace FE::AST
{
    /*
     * Lab 3-1: 语义分析器 (Semantic Analyzer)
     *
     * 本文件定义了 ASTChecker 类，用于遍历 AST 并执行语义检查。
     * 大部分 visit 方法已改为 TODO，需要你自己实现。
     *
     * 已保留的示例实现：
     * - libFuncRegister(): 注册 SysY 标准库函数
     * - visit(LiteralExpr&): 字面量表达式检查
     * - visit(ExprStmt&): 表达式语句检查
     * - visit(Initializer&): 初始化器检查
     * - visit(InitializerList&): 初始化器列表检查
     *
     * 需要实现的主要功能：
     * 1. 符号表管理 (作用域进入/退出，符号查找/添加)
     * 2. 类型检查 (变量类型、函数参数匹配、返回值检查等)
     * 3. 语义错误检测 (重定义、未定义、非法操作等)
     * 4. 常量折叠 (编译期常量计算)
     * 5. 控制流检查 (break/continue 在循环内，return 在函数内等)
     */

    // 此处的 Visitor_t<bool> 声明访问者返回 bool 类型，不需要函数参数
    // 我们使用它返回的 bool 来表示语义检查是否通过。如果存在语义错误，则返回 false
    using Checker_t = Visitor_t<bool>;

    class ASTChecker : public Checker_t
    {
      private:
        FE::Sym::SymTable                        symTable; // 符号表实例
        std::map<FE::Sym::Entry*, VarAttr>       glbSymbols; // 全局变量符号表快照
        std::map<FE::Sym::Entry*, FuncDeclStmt*> funcDecls; // 已声明的函数列表

        bool mainExists; // 是否存在 main 函数

        bool  funcHasReturn; // 当前函数是否包含返回语句
        Type* curFuncRetType; // 当前函数的返回类型

        size_t loopDepth; // 当前循环嵌套深度，用于检查 break/continue

      public:
        std::vector<std::string> errors; // 语义错误列表

        ASTChecker()
            : symTable(),
              glbSymbols(),
              funcDecls(),
              mainExists(false),
              funcHasReturn(false),
              curFuncRetType(voidType),
              loopDepth(0),
              errors()
        {
            libFuncRegister();
        }

        ~ASTChecker()
        {
            const char* libFuncNames[] = {"getint",
                "getch",
                "getarray",
                "getfloat",
                "getfarray",
                "putint",
                "putch",
                "putarray",
                "putfloat",
                "putfarray",
                "_sysy_starttime",
                "_sysy_stoptime"};

            for (const char* name : libFuncNames)
            {
                FE::Sym::Entry* entry = FE::Sym::Entry::getEntry(name);
                auto            it    = funcDecls.find(entry);
                if (it != funcDecls.end()) delete it->second;
            }
        }

      public:
        const std::map<FE::Sym::Entry*, VarAttr>&       getGlbSymbols() const { return glbSymbols; }
        const std::map<FE::Sym::Entry*, FuncDeclStmt*>& getFuncDecls() const { return funcDecls; }

      private:
        // Basic AST nodes
        bool visit(Root& node) override;

        // Declaration nodes
        bool visit(Initializer& node) override;
        bool visit(InitializerList& node) override;
        bool visit(VarDeclarator& node) override;
        bool visit(ParamDeclarator& node) override;
        bool visit(VarDeclaration& node) override;

        // Expression nodes
        bool visit(LeftValExpr& node) override;
        bool visit(LiteralExpr& node) override;
        bool visit(UnaryExpr& node) override;
        bool visit(BinaryExpr& node) override;
        bool visit(CallExpr& node) override;
        bool visit(CommaExpr& node) override;

        // Statement nodes
        bool visit(ExprStmt& node) override;
        bool visit(FuncDeclStmt& node) override;
        bool visit(VarDeclStmt& node) override;
        bool visit(BlockStmt& node) override;
        bool visit(ReturnStmt& node) override;
        bool visit(WhileStmt& node) override;
        bool visit(IfStmt& node) override;
        bool visit(BreakStmt& node) override;
        bool visit(ContinueStmt& node) override;
        bool visit(ForStmt& node) override;

        // 该函数向符号表中注册 SysY 的库函数，以便在语义检查时获取它们的信息
        // 示例实现已提供，展示如何创建函数声明并加入 funcDecls
        void libFuncRegister();

      private:
        // 这两个辅助函数用于对一元/二元表达式做类型推断与常量折叠（若可能）
        // Const Decl 的变量以及 LiteralExpr 节点中的字面量都可被视为编译期常量
        // 对单元表达式，若 isConstexpr 为 true，则返回的 Value 也为编译期常量
        ExprValue typeInfer(const ExprValue& operand, Operator op, const Node& node, bool& hasError);
        // 对二元表达式，若 lhs 与 rhs 的 isConstexpr 都为 true，则返回的 Value 也为编译期常量
        ExprValue typeInfer(const ExprValue& lhs, const ExprValue& rhs, Operator op, const Node& node, bool& hasError);
    };
}  // namespace FE::AST

#endif  // __FRONTEND_AST_VISITOR_SEMENTIC_CHECK_AST_CHECKER_H__
