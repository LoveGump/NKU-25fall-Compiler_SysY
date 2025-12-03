#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(LeftValExpr& node)
    {
        // TODO(Lab3-1): 实现左值表达式的语义检查
        // 检查变量是否存在，处理数组下标访问，进行类型检查和常量折叠

        if (!node.entry)
        {
            // 无效的左值表达式
            this->errors.emplace_back("Invalid left value expression at line " + std::to_string(node.line_num));
            return false;
        }
        // 检查变量是否存在于符号表中
        auto* varAttr = this->symTable.getSymbol(node.entry);
        if (!varAttr)
        {
            this->errors.emplace_back(
                "Undeclared variable '" + node.entry->getName() + "' at line " + std::to_string(node.line_num));
            return false;
        }

        bool success = true;
        // 处理数组下标访问，数组访问表达式
        size_t idxCnt = node.indices ? node.indices->size() : 0;
        if (idxCnt > 0)
        {
            // 是数组，访问数组下标表达式，进行类型检查
            for (auto* indexExpr : *(node.indices))
            {
                if (!indexExpr) continue;
                success = apply(*this, *indexExpr) && success;
                // 检查下标类型是否为整数类型，或者可以隐式转换为整型
                Type* idxType = indexExpr->attr.val.value.type;
                if (!idxType || idxType->getTypeGroup() == TypeGroup::POINTER || idxType->getBaseType() == Type_t::VOID)
                {
                    // 下标表达式必须为整型
                    errors.emplace_back("Array index must be integer at line " + std::to_string(indexExpr->line_num));
                    return false;
                }
            }
            // 访问的数组维度，不能超过声明的维度
            if (idxCnt > varAttr->arrayDims.size())
            {
                this->errors.emplace_back("Too many indices for array variable '" + node.entry->getName() +
                                          "' at line " + std::to_string(node.line_num));
                return false;
            }
        }

        // 设置类型：未完全索引的数组在表达式中视为指向元素类型的指针
        if (!varAttr->arrayDims.empty())
        {
            // 数组类型处理
            if (idxCnt == 0)  // 未索引，视为指针类型
                node.attr.val.value.type = TypeFactory::getPtrType(varAttr->type);
            else if (idxCnt < varAttr->arrayDims.size())  // 部分索引，退化为指针类型
                node.attr.val.value.type = TypeFactory::getPtrType(varAttr->type);
            else
                node.attr.val.value.type = varAttr->type;  // 完全索引到元素
        }
        else
        {
            // 非数组类型处理
            if (idxCnt > 0)
            {
                // 非数组类型却有下标访问，报错
                errors.emplace_back("Subscripted value is not an array: '" + node.entry->getName() + "' at line " +
                                    std::to_string(node.line_num));
                success                   = false;
                node.attr.val.value.type  = voidType;
                node.attr.val.isConstexpr = false;
                return false;
            }
            node.attr.val.value.type = varAttr->type;
        }

        // 常量折叠：仅对非数组 const 且存在单一编译期初始化值作折叠
        if (varAttr->isConstDecl && varAttr->arrayDims.empty() && varAttr->initList.size() == 1)
        {
            node.attr.val.value       = varAttr->initList[0];
            node.attr.val.isConstexpr = true;
        }
        else { node.attr.val.isConstexpr = false; }

        return success;
    }

    bool ASTChecker::visit(LiteralExpr& node)
    {
        // 示例实现：字面量表达式的语义检查
        // 字面量总是编译期常量，直接设置属性
        node.attr.val.isConstexpr = true;
        node.attr.val.value       = node.literal;
        return true;
    }

    bool ASTChecker::visit(UnaryExpr& node)
    {
        // TODO(Lab3-1): 实现一元表达式的语义检查
        // 访问子表达式，检查操作数类型，调用类型推断函数

        // 访问子表达式并进行类型推断/常量折叠
        bool success = true;
        if (!node.expr)
        {
            // 无效操作数
            errors.emplace_back("Null operand for unary expression at line " + std::to_string(node.line_num));
            return false;
        }

        success &= apply(*this, *node.expr);  // 访问子表达式并进行类型推断/常量折叠

        // 早期检查：禁止 void 作为一元运算的操作数
        Type* ety = node.expr->attr.val.value.type;
        if (!ety || ety->getBaseType() == Type_t::VOID)
        {
            errors.emplace_back("Void value used with unary operator " + toString(node.op) + " at line " +
                                std::to_string(node.line_num));
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        bool      hasError = false;
        ExprValue result   = typeInfer(node.expr->attr.val, node.op, node, hasError);
        node.attr.op       = node.op;
        node.attr.val      = result;
        return success && !hasError;
    }

    bool ASTChecker::visit(BinaryExpr& node)
    {
        // TODO(Lab3-1): 实现二元表达式的语义检查
        // 访问左右子表达式，检查操作数类型，调用类型推断

        // 访问左右子表达式并进行类型推断/常量折叠
        bool success = true;
        if (!node.lhs || !node.rhs)
        {
            // 无效操作数
            errors.emplace_back("Incomplete binary expression at line " + std::to_string(node.line_num));
            return false;
        }

        // 检查左右操作数
        success &= apply(*this, *node.lhs);
        success &= apply(*this, *node.rhs);

        // 禁止 void 作为二元运算任一操作数
        Type* lty = node.lhs->attr.val.value.type;
        Type* rty = node.rhs->attr.val.value.type;
        if (!lty || lty->getBaseType() == Type_t::VOID || !rty || rty->getBaseType() == Type_t::VOID)
        {
            errors.emplace_back("Void value used in binary operator " + toString(node.op) + " at line " +
                                std::to_string(node.line_num));
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        // 赋值的额外检查：左侧必须是可赋值的左值，且不可为 const
        if (node.op == Operator::ASSIGN)
        {
            // 检查左操作数是否为左值
            auto* lval = dynamic_cast<LeftValExpr*>(node.lhs);
            if (!lval)
            {
                // 非左值不可赋值
                errors.emplace_back(
                    "Left operand of assignment is not assignable at line " + std::to_string(node.line_num));
                success = false;
            }
            else
            {
                // 检查左值是否为 const 变量
                auto* lattr = symTable.getSymbol(lval->entry);
                if (lattr && lattr->isConstDecl)
                {
                    // const 变量不可赋值
                    errors.emplace_back("Cannot assign to const variable '" + lval->entry->getName() + "' at line " +
                                        std::to_string(node.line_num));
                    success = false;
                }
            }

            // 类型兼容检查
            Type* lty2 = node.lhs->attr.val.value.type;
            Type* rty2 = node.rhs->attr.val.value.type;
            if (lty2 && rty2)
            {
                bool lhsArrayLike = lty2->getTypeGroup() == TypeGroup::POINTER;  // 数组在表达式中的退化形态
                bool rhsArrayLike = rty2->getTypeGroup() == TypeGroup::POINTER;
                if (lhsArrayLike != rhsArrayLike)
                {
                    errors.emplace_back(
                        "Assignment type mismatch (array vs scalar) at line " + std::to_string(node.line_num));
                    success = false;
                }

                if (lhsArrayLike && rhsArrayLike && lty2 != rty2)
                {
                    errors.emplace_back(
                        "Assignment type mismatch (different pointer types) at line " + std::to_string(node.line_num));
                    success = false;
                }
            }
            // 基本类型可以隐式类型转换
        }

        bool      hasError = false;
        ExprValue result   = typeInfer(node.lhs->attr.val, node.rhs->attr.val, node.op, node, hasError);
        node.attr.op       = node.op;
        node.attr.val      = result;
        return success && !hasError;
    }

    bool ASTChecker::visit(CallExpr& node)
    {
        // TODO(Lab3-1): 实现函数调用表达式的语义检查
        // 检查函数是否存在，访问实参列表，检查参数数量和类型匹配

        if (!node.func)
        {
            // 无效函数标识符
            errors.emplace_back("Invalid function call at line " + std::to_string(node.line_num));
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        // 检查函数是否存在于函数声明表中
        auto it = funcDecls.find(node.func);
        if (it == funcDecls.end() || !it->second)
        {
            // 函数要先被定义
            errors.emplace_back(
                "Undefined function '" + node.func->getName() + "' at line " + std::to_string(node.line_num));
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        // 获取函数声明
        FuncDeclStmt* decl = it->second;

        // 访问实参
        bool   success  = true;
        size_t argCount = node.args ? node.args->size() : 0;
        if (node.args)
        {
            // 访问每个实参表达式
            for (auto* a : *node.args)
            {
                // 访问实参
                if (!a) continue;
                success &= apply(*this, *a);
            }
        }

        // 参数数量检查
        size_t paramCount = decl->params ? decl->params->size() : 0;
        if (argCount != paramCount)
        {
            // 参数数量不匹配
            errors.emplace_back("Argument count mismatch in call to '" + node.func->getName() + "' at line " +
                                std::to_string(node.line_num));
            success = false;
        }

        // 类型匹配检查
        if (success && node.args && decl->params)
        {
            for (size_t i = 0; i < paramCount && i < argCount; ++i)
            {
                // 获取形参和实参类型
                auto*      p      = (*decl->params)[i];
                Type*      pty    = p->type;
                const bool isArr  = (p->dims && !p->dims->empty());
                Type* expected    = isArr ? TypeFactory::getPtrType(pty) : pty;  // 形参为数组时视为指针类型
                ExprNode* argExpr = (*node.args)[i];
                Type*     actual  = argExpr ? argExpr->attr.val.value.type : voidType;

                if (!expected || !actual)
                {
                    success = false;
                    errors.emplace_back("Invalid parameter/argument type at line " + std::to_string(node.line_num));
                    continue;
                }

                // 禁止将 void 作为实参传入任何参数
                if (actual->getBaseType() == Type_t::VOID)
                {
                    success = false;
                    errors.emplace_back("Void value passed to parameter " + std::to_string(i) + " in call to '" +
                                        node.func->getName() + "' at line " + std::to_string(node.line_num));
                    continue;
                }

                // 简化的匹配规则：
                // - 期望指针 => 实参必须是指针类型
                // - 期望基础类型 => 实参为基础类型即可（允许不同数值类型之间的转换）
                if (expected->getTypeGroup() == TypeGroup::POINTER)
                {
                    if (actual->getTypeGroup() != TypeGroup::POINTER)
                    {
                        errors.emplace_back("Argument type mismatch for parameter " + std::to_string(i) +
                                            " in call to '" + node.func->getName() + "' at line " +
                                            std::to_string(node.line_num));
                        success = false;
                    }
                }
                else
                {
                    if (actual->getTypeGroup() == TypeGroup::POINTER)
                    {
                        success = false;
                        errors.emplace_back("Pointer passed to non-pointer parameter " + std::to_string(i) +
                                            " in call to '" + node.func->getName() + "' at line " +
                                            std::to_string(node.line_num));
                    }
                }
            }
        }

        node.attr.val.value.type  = decl->retType;
        node.attr.val.isConstexpr = false;
        return success;
    }

    bool ASTChecker::visit(CommaExpr& node)
    {
        // TODO(Lab3-1): 实现逗号表达式的语义检查
        // 依序访问各子表达式，结果为最后一个表达式的属性

        if (!node.exprs || node.exprs->empty()) return true;
        bool success = true;

        for (auto* e : *node.exprs)
        {
            if (!e) continue;
            success &= apply(*this, *e);
        }
        node.attr.val = (*node.exprs)[node.exprs->size() - 1]->attr.val;
        return success;
    }
}  // namespace FE::AST
