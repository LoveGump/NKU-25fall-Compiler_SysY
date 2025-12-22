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
        if (!node.entry)
        {
            // 无效函数名
            this->errors.emplace_back("Invalid function name at line " + std::to_string(node.line_num));
            return false;
        }
        bool success = true;  // 默认成功
        // 进入新作用域
        this->symTable.enterScope();

        // 处理形参
        if (node.params)
        {
            for (auto* param : *(node.params)) { success = apply(*this, *param) && success; }
        }

        // 设置当前函数返回类型
        this->funcHasReturn  = false;
        this->curFuncRetType = node.retType ? node.retType : voidType;

        // 访问函数体

        if (node.body) { success = apply(*this, *node.body) && success; }

        if (success && node.entry->getName() != "main" && this->curFuncRetType != voidType && !this->funcHasReturn)
        {
            // 非void函数缺少return语句
            this->errors.emplace_back("Function '" + node.entry->getName() + "' missing return statement at line " +
                                      std::to_string(node.line_num));
            success = false;
        }

        // 退出作用域
        this->symTable.exitScope();

        return success;
    }

    bool ASTChecker::visit(VarDeclStmt& node)
    {
        // TODO(Lab3-1): 实现变量声明语句的语义检查
        // 空声明直接通过，否则委托给变量声明处理
        if (!node.decl) return true;
        return apply(*this, *node.decl);
    }

    bool ASTChecker::visit(BlockStmt& node)
    {
        // TODO(Lab3-1): 实现块语句的语义检查
        // 进入新作用域，逐条访问语句，退出作用域
        this->symTable.enterScope();
        bool success = true;
        if (node.stmts)
        {
            for (auto* stmt : *(node.stmts))
            {
                if (!stmt) continue;
                // 函数定义只能出现在全局作用域，其他地方出现报错
                if (auto* funcDecl = dynamic_cast<FuncDeclStmt*>(stmt))
                {
                    this->errors.emplace_back(
                        "Function definition not allowed here at line " + std::to_string(funcDecl->line_num));
                    success = false;
                    continue;
                }
                success = apply(*this, *stmt) && success;
            }
        }
        this->symTable.exitScope();
        return success;
    }

    bool ASTChecker::visit(ReturnStmt& node)
    {
        // TODO(Lab3-1): 实现返回语句的语义检查
        // 设置返回标记，检查作用域，检查返回值类型匹配
        this->funcHasReturn = true;
        bool success        = true;
        if (curFuncRetType == voidType)
        {
            // 当前函数为void类型，返回语句不应有返回值
            if (node.retExpr)
            {
                this->errors.emplace_back(
                    "Return statement with a value in void function at line " + std::to_string(node.line_num));
                success = false;
            }
            return success;
        }

        // 非void函数，必须有返回值且类型匹配
        if (!node.retExpr)
        {
            this->errors.emplace_back(
                "Return statement missing a value in non-void function at line " + std::to_string(node.line_num));
            return false;
        }
        success = apply(*this, *node.retExpr) && success;

        Type* exprType = node.retExpr->attr.val.value.type;
        if (!exprType)
        {
            // 无法确定返回值类型
            this->errors.emplace_back(
                "Cannot determine return expression type at line " + std::to_string(node.line_num));
            return false;
        }
        // 匹配规则：允许隐式类型转换（如int到float），但不允许不兼容类型
        if (this->curFuncRetType->getTypeGroup() == TypeGroup::POINTER ||
            exprType->getTypeGroup() == TypeGroup::POINTER)
        {
            // 指针类型必须完全匹配
            if (this->curFuncRetType != exprType)
            {
                this->errors.emplace_back("Return type mismatch at line " + std::to_string(node.line_num));
                return false;
            }
            return success;
        }
        if (exprType->getBaseType() == Type_t::VOID)
        {
            // 返回值不能为void类型
            this->errors.emplace_back("Return expression cannot be void type at line " + std::to_string(node.line_num));
            return false;
        }
        // 基础类型允许隐式转换

        return success;
    }

    bool ASTChecker::visit(WhileStmt& node)
    {
        // TODO(Lab3-1): 实现while循环的语义检查
        // 检查作用域，访问条件表达式，管理循环深度，访问循环体
        bool success = true;
        if (!node.cond)
        {
            this->errors.emplace_back("While statement missing condition at line " + std::to_string(node.line_num));
            return false;
        }
        // 访问条件表达式
        success = apply(*this, *node.cond) && success;
        // 条件表达式类型必须为bool 或者可转换为bool
        Type* condType = node.cond->attr.val.value.type;
        if (!condType)
        {
            this->errors.emplace_back("Cannot determine while condition type at line " + std::to_string(node.line_num));
            return false;
        }
        if (condType->getBaseType() == Type_t::VOID)
        {
            this->errors.emplace_back("While condition cannot be void type at line " + std::to_string(node.line_num));
            return false;
        }
        // 管理循环深度
        this->loopDepth++;
        // 访问循环体
        if (node.body) { success = apply(*this, *node.body) && success; }
        this->loopDepth--;
        return success;
    }

    bool ASTChecker::visit(IfStmt& node)
    {
        // TODO(Lab3-1): 实现if语句的语义检查
        // 检查作用域，访问条件表达式，分别访问then和else分支
        bool success = true;
        if (!node.cond)
        {
            this->errors.emplace_back("If statement missing condition at line " + std::to_string(node.line_num));
            return false;
        }
        // 访问条件表达式
        success = apply(*this, *node.cond) && success;
        // 条件表达式类型必须为bool 或者可转换为bool
        Type* condType = node.cond->attr.val.value.type;
        if (!condType)
        {
            this->errors.emplace_back("Cannot determine if condition type at line " + std::to_string(node.line_num));
            return false;
        }
        if (condType->getBaseType() == Type_t::VOID)
        {
            this->errors.emplace_back("If condition cannot be void type at line " + std::to_string(node.line_num));
            return false;
        }
        // 访问then分支
        if (node.thenStmt) { success = apply(*this, *node.thenStmt) && success; }
        // 访问else分支
        if (node.elseStmt) { success = apply(*this, *node.elseStmt) && success; }
        return success;
    }

    bool ASTChecker::visit(BreakStmt& node)
    {
        // TODO(Lab3-1): 实现break语句的语义检查
        // 检查是否在循环内使用
        if (this->loopDepth == 0)
        {
            this->errors.emplace_back("Break statement not within a loop at line " + std::to_string(node.line_num));
            return false;
        }
        return true;
    }

    bool ASTChecker::visit(ContinueStmt& node)
    {
        // TODO(Lab3-1): 实现continue语句的语义检查
        // 检查是否在循环内使用
        if (this->loopDepth == 0)
        {
            this->errors.emplace_back("Continue statement not within a loop at line " + std::to_string(node.line_num));
            return false;
        }
        return true;
    }

    // for 语句的语义检查,
    bool ASTChecker::visit(ForStmt& node)
    {
        // TODO(Lab3-1): 实现for循环的语义检查
        // 检查作用域，访问初始化、条件、步进表达式，管理循环深度
        bool success = true;
        // 进入新作用域
        this->symTable.enterScope();
        // 访问初始化语句
        if (node.init) { success = apply(*this, *node.init) && success; }
        // 访问条件表达式
        if (!node.cond)
        {
            this->errors.emplace_back("For statement missing condition at line " + std::to_string(node.line_num));
            this->symTable.exitScope();
            return false;
        }
        success = apply(*this, *node.cond) && success;
        // 条件表达式类型必须为bool 或者可转换为bool
        Type* condType = node.cond->attr.val.value.type;
        if (!condType)
        {
            this->errors.emplace_back("Cannot determine for condition type at line " + std::to_string(node.line_num));
            this->symTable.exitScope();
            return false;
        }
        if (condType->getBaseType() == Type_t::VOID)
        {
            this->errors.emplace_back("For condition cannot be void type at line " + std::to_string(node.line_num));
            this->symTable.exitScope();
            return false;
        }
        // 管理循环深度
        this->loopDepth++;
        // 访问步进表达式
        if (node.step) { success = apply(*this, *node.step) && success; }
        // 访问循环体
        if (node.body) { success = apply(*this, *node.body) && success; }
        this->loopDepth--;
        // 退出作用域
        this->symTable.exitScope();
        return success;
    }
}  // namespace FE::AST
