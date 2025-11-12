#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(ExprStmt& node)
    {
        // 示例实现：表达式语句的语义检查
        // 空表达式直接通过，否则访问内部表达式
        if (!node.expr) return true;
        return apply(*this, *node.expr);
    }

    bool ASTChecker::visit(FuncDeclStmt& node)
    {
        // TODO(Lab3-1): 实现函数声明的语义检查
        // 检查作用域，记录函数信息，处理形参和函数体，检查返回语句
        // (void)node;
        // TODO("Lab3-1: Implement FuncDeclStmt semantic checking");

        // 函数声明语义检查：
        // Root 已收集所有函数：
        // 1) 不再在这里向 funcDecls 插入/做重定义判断（避免重复）
        // 2) 进入函数作用域：将形参加入符号表
        // 3) 检查函数体：跟踪返回语句并验证返回类型

        if (!node.entry)
        {
            // 无效函数名
            errors.emplace_back("Invalid function name at line " + std::to_string(node.line_num));
            return false;
        }

        // Root::visit 已经收集了函数到 funcDecls

        // 进入函数作用域
        symTable.enterScope();

        // 处理形参：退化数组为指针类型，加入符号表。检查重定义。
        if (node.params)
        {
            // 遍历形参列表
            for (auto* p : *node.params)
            {
                if (!p || !p->entry)
                {
                    // 无效形参
                    errors.emplace_back("Invalid parameter in function '" + node.entry->getName() + "'");
                    symTable.exitScope();
                    return false;
                }
                VarAttr attr;   // 形参属性
                attr.type        = p->type; // 基础类型
                attr.isConstDecl = false; // 是否为常量声明
                attr.scopeLevel  = symTable.getScopeDepth(); // 作用域层级
                attr.arrayDims.clear();
                if (p->dims && !p->dims->empty())
                {
                    // 数组形参退化为指针
                    attr.type = TypeFactory::getPtrType(p->type);
                }
                // 重复参数检查
                if (symTable.getSymbol(p->entry))
                {
                    errors.emplace_back("Redefinition of parameter '" + p->entry->getName() + "' in function '" +
                                        node.entry->getName() + "' at line " + std::to_string(p->line_num));
                    symTable.exitScope();
                    return false;
                }
                symTable.addSymbol(p->entry, attr);
            }
        }

        // 设置函数返回类型环境
        funcHasReturn  = false;
        curFuncRetType = node.retType ? node.retType : voidType;

        // 检查函数体
        bool ok = true;
        if (node.body) { ok &= apply(*this, *node.body); }

        // 如果返回类型非 void，则必须出现有效返回语句
        if (ok && curFuncRetType != voidType && !funcHasReturn)
        {
            errors.emplace_back("Missing return statement in function '" + node.entry->getName() + "'");
            ok = false;
        }

        // 退出函数作用域
        symTable.exitScope();
        return ok;
    }

    bool ASTChecker::visit(VarDeclStmt& node)
    {
        // TODO(Lab3-1): 实现变量声明语句的语义检查
        // 空声明直接通过，否则委托给变量声明处理
        // (void)node;
        // TODO("Lab3-1: Implement VarDeclStmt semantic checking");
        // 变量声明语句：委托 VarDeclaration
        if (!node.decl) return true;
        return apply(*this, *node.decl);
    }

    bool ASTChecker::visit(BlockStmt& node)
    {
        // TODO(Lab3-1): 实现块语句的语义检查
        // 进入新作用域，逐条访问语句，退出作用域
        // (void)node;
        // TODO("Lab3-1: Implement BlockStmt semantic checking");
        // 进入块作用域，逐条访问后退出
        symTable.enterScope();
        bool ok = true;
        if (node.stmts)
        {
            for (auto* s : *node.stmts)
            {
                if (!s) continue;
                // 规则：函数定义只能出现在根节点（CompUnit）层级，块内禁止
                if (auto* f = dynamic_cast<FuncDeclStmt*>(s))
                {
                    errors.emplace_back(
                        "Function definition is not allowed inside a block at line " + std::to_string(f->line_num));
                    ok = false;
                    // 不深入检查该函数，避免错误连带
                    continue;
                }
                ok &= apply(*this, *s);
            }
        }
        symTable.exitScope();
        return ok;
    }

    bool ASTChecker::visit(ReturnStmt& node)
    {
        // TODO(Lab3-1): 实现返回语句的语义检查
        // 设置返回标记，检查作用域，检查返回值类型匹配
        // (void)node;
        // TODO("Lab3-1: Implement ReturnStmt semantic checking");
        // 返回语句：标记并检查返回值类型匹配
        funcHasReturn = true;
        bool ok       = true;
        if (curFuncRetType == voidType)
        {
            // void 函数不应返回值
            if (node.retExpr)
            {
                ok = false;
                errors.emplace_back("Void function should not return a value at line " + std::to_string(node.line_num));
            }
            return ok;
        }

        // 非 void：必须有表达式且类型兼容
        if (!node.retExpr)
        {
            errors.emplace_back("Non-void function missing return value at line " + std::to_string(node.line_num));
            return false;
        }

        ok &= apply(*this, *node.retExpr);
        Type* actual = node.retExpr->attr.val.value.type;
        if (!actual)
        {
            errors.emplace_back("Invalid return expression at line " + std::to_string(node.line_num));
            return false;
        }

        // 基础匹配规则：允许数值类型间兼容，拒绝指针/void不匹配
        if (curFuncRetType->getTypeGroup() == TypeGroup::POINTER || actual->getTypeGroup() == TypeGroup::POINTER)
        {
            if (curFuncRetType != actual)
            {
                errors.emplace_back("Return type mismatch at line " + std::to_string(node.line_num));
                ok = false;
            }
        }
        else
        {
            if (actual->getBaseType() == Type_t::VOID)
            {
                errors.emplace_back("Return expression cannot be void at line " + std::to_string(node.line_num));
                ok = false;
            }
            // 基础类型：不做严格区分（int/ll/float/bool之间视为兼容）
        }
        return ok;
    }

    bool ASTChecker::visit(WhileStmt& node)
    {
        // TODO(Lab3-1): 实现while循环的语义检查
        // 检查作用域，访问条件表达式，管理循环深度，访问循环体
        // (void)node;
        // TODO("Lab3-1: Implement WhileStmt semantic checking");
        bool ok = true;
        if (!node.cond)
        {
            errors.emplace_back("While missing condition at line " + std::to_string(node.line_num));
            return false;
        }
        ok &= apply(*this, *node.cond);

        // 条件应为可转换为布尔
        Type* cty = node.cond->attr.val.value.type;
        if (!cty)
        {
            errors.emplace_back("Invalid while condition at line " + std::to_string(node.line_num));
            ok = false;
        }
        else if (cty->getBaseType() == Type_t::VOID)
        {
            errors.emplace_back("While condition cannot be void at line " + std::to_string(node.line_num));
            ok = false;
        }

        ++loopDepth;
        if (node.body) ok &= apply(*this, *node.body);
        --loopDepth;
        return ok;
    }

    bool ASTChecker::visit(IfStmt& node)
    {
        // TODO(Lab3-1): 实现if语句的语义检查
        // 检查作用域，访问条件表达式，分别访问then和else分支
        // (void)node;
        // TODO("Lab3-1: Implement IfStmt semantic checking");
        bool ok = true;
        if (!node.cond)
        {
            errors.emplace_back("If missing condition at line " + std::to_string(node.line_num));
            return false;
        }
        ok &= apply(*this, *node.cond);
        // If 条件不允许为 void
        {
            Type* cty = node.cond->attr.val.value.type;
            if (!cty)
            {
                errors.emplace_back("Invalid if condition at line " + std::to_string(node.line_num));
                ok = false;
            }
            else if (cty->getBaseType() == Type_t::VOID)
            {
                errors.emplace_back("If condition cannot be void at line " + std::to_string(node.line_num));
                ok = false;
            }
        }
        if (node.thenStmt) ok &= apply(*this, *node.thenStmt);
        if (node.elseStmt) ok &= apply(*this, *node.elseStmt);
        return ok;
    }

    bool ASTChecker::visit(BreakStmt& node)
    {
        // TODO(Lab3-1): 实现break语句的语义检查
        // 检查是否在循环内使用
        // (void)node;
        // TODO("Lab3-1: Implement BreakStmt semantic checking");
        if (loopDepth == 0)
        {
            errors.emplace_back("break used outside of loop at line " + std::to_string(node.line_num));
            return false;
        }
        return true;
    }

    bool ASTChecker::visit(ContinueStmt& node)
    {
        // TODO(Lab3-1): 实现continue语句的语义检查
        // 检查是否在循环内使用
        // (void)node;
        // TODO("Lab3-1: Implement ContinueStmt semantic checking");
        if (loopDepth == 0)
        {
            errors.emplace_back("continue used outside of loop at line " + std::to_string(node.line_num));
            return false;
        }
        return true;
    }

    bool ASTChecker::visit(ForStmt& node)
    {
        // TODO(Lab3-1): 实现for循环的语义检查
        // 检查作用域，访问初始化、条件、步进表达式，管理循环深度
        // (void)node;
        // TODO("Lab3-1: Implement ForStmt semantic checking");
        bool ok = true;
        symTable.enterScope();
        if (node.init) ok &= apply(*this, *node.init);
        if (node.cond) ok &= apply(*this, *node.cond);
        ++loopDepth;
        if (node.body) ok &= apply(*this, *node.body);
        --loopDepth;
        if (node.step) ok &= apply(*this, *node.step);
        symTable.exitScope();
        return ok;
    }
}  // namespace FE::AST
