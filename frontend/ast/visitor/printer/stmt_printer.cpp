#include "ast_printer.h"
#include <frontend/symbol/symbol_entry.h>

namespace FE::AST
{
    void ASTPrinter::visit(ExprStmt& node, std::ostream* os)
    {

        emitHeader(*os, std::string("ExprStmt line: ") + std::to_string(node.line_num));
        if (!node.expr) return;
        withChild(true, [&]() { apply(*this, *node.expr, os); });
    }

    void ASTPrinter::visit(FuncDeclStmt& node, std::ostream* os)
    {
        std::string sig = std::string("FuncDecl ") + node.entry->getName() + "(";
        if (node.params && !node.params->empty())
        {
            bool first = true;
            for (auto* p : *node.params)
            {
                if (!p) continue;
                if (!first) sig += ", ";
                first = false;

                std::string paramStr = p->type->toString();
                paramStr += " ";
                paramStr += p->entry->getName();

                if (p->dims)
                {
                    for (size_t j = 0; j < p->dims->size(); ++j)
                    {
                        ExprNode* dimExpr = (*p->dims)[j];
                        // 维度可能是字面量、标识符或更复杂的常量表达式，解析阶段尚未折叠。
                        if (auto* lit = dynamic_cast<LiteralExpr*>(dimExpr))
                        {
                            int v = lit->literal.getInt();
                            if (v < 0)
                                paramStr += "[]";  // 省略首维
                            else
                                paramStr += "[" + std::to_string(v) + "]";
                        }
                        else if (auto* lv = dynamic_cast<LeftValExpr*>(dimExpr))
                        {
                            // 使用标识符名字展示，语义阶段再判定是否为常量表达式
                            paramStr += "[" + lv->entry->getName() + "]";
                        }
                        else
                        {
                            // 其它表达式暂时用占位符，避免非法转换导致崩溃
                            paramStr += "[?]";
                        }
                    }
                }

                sig += paramStr;
            }
        }
        sig += ") -> ";
        sig += node.retType->toString();
        sig += ", line: ";
        sig += std::to_string(node.line_num);
        emitHeader(*os, sig);

        if (node.body) withChild(true, [&]() { apply(*this, *node.body, os); });
    }

    void ASTPrinter::visit(VarDeclStmt& node, std::ostream* os)
    {

        emitHeader(*os, "VarDeclStmt");
        if (!node.decl) return;
        withChild(true, [&]() { apply(*this, *node.decl, os); });
    }

    void ASTPrinter::visit(BlockStmt& node, std::ostream* os)
    {

        emitHeader(*os, std::string("BlockStmt, line: ") + std::to_string(node.line_num));
        if (!node.stmts) return;
        size_t cnt = node.stmts->size();
        for (size_t i = 0; i < cnt; ++i)
        {
            auto* stmt = (*node.stmts)[i];
            if (!stmt) continue;
            withChild(i + 1 == cnt, [&]() { apply(*this, *stmt, os); });
        }
    }

    void ASTPrinter::visit(ReturnStmt& node, std::ostream* os)
    {

        emitHeader(*os, "ReturnStmt");
        if (!node.retExpr) return;
        withChild(true, [&]() { apply(*this, *node.retExpr, os); });
    }

    void ASTPrinter::visit(WhileStmt& node, std::ostream* os)
    {

        emitHeader(*os, "WhileStmt");
        if (node.cond)
        {
            withChild(false, [&]() {
                emitHeader(*os, "Condition:");
                withChild(true, [&]() { apply(*this, *node.cond, os); });
            });
        }
        if (node.body)
        {
            withChild(true, [&]() {
                emitHeader(*os, "Body:");
                withChild(true, [&]() { apply(*this, *node.body, os); });
            });
        }
    }

    void ASTPrinter::visit(IfStmt& node, std::ostream* os)
    {

        emitHeader(*os, "IfStmt");
        if (node.cond)
        {
            withChild(false, [&]() {
                emitHeader(*os, "Condition:");
                withChild(true, [&]() { apply(*this, *node.cond, os); });
            });
        }
        if (node.thenStmt)
        {
            withChild(false, [&]() {
                emitHeader(*os, "Then:");
                withChild(true, [&]() { apply(*this, *node.thenStmt, os); });
            });
        }
        if (node.elseStmt)
        {
            withChild(true, [&]() {
                emitHeader(*os, "Else:");
                withChild(true, [&]() { apply(*this, *node.elseStmt, os); });
            });
        }
    }

    void ASTPrinter::visit(BreakStmt& node, std::ostream* os)
    {
        (void)node;

        emitHeader(*os, "BreakStmt");
    }

    void ASTPrinter::visit(ContinueStmt& node, std::ostream* os)
    {
        (void)node;

        emitHeader(*os, "ContinueStmt");
    }

    void ASTPrinter::visit(ForStmt& node, std::ostream* os)
    {

        emitHeader(*os, std::string("ForStmt, line: ") + std::to_string(node.line_num));

        if (node.init)
        {
            withChild(false, [&]() {
                emitHeader(*os, "Init:");
                withChild(true, [&]() { apply(*this, *node.init, os); });
            });
        }
        if (node.cond)
        {
            withChild(false, [&]() {
                emitHeader(*os, "Condition:");
                withChild(true, [&]() { apply(*this, *node.cond, os); });
            });
        }
        if (node.step)
        {
            withChild(false, [&]() {
                emitHeader(*os, "Step:");
                withChild(true, [&]() { apply(*this, *node.step, os); });
            });
        }
        if (node.body)
        {
            withChild(true, [&]() {
                emitHeader(*os, "Body:");
                withChild(true, [&]() { apply(*this, *node.body, os); });
            });
        }
    }
}  // namespace FE::AST
