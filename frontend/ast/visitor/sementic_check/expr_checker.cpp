#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(LeftValExpr& node)
    {
        // TODO(Lab3-1): 实现左值表达式的语义检查
        // 检查变量是否存在，处理数组下标访问，进行类型检查和常量折叠
        // (void)node;
        // TODO("Lab3-1: Implement LeftValExpr semantic checking");

        // 左值表达式语义检查：
        // 1) 符号存在性检查
        // 2) 数组下标检查（类型与数量）
        // 3) 设置表达式类型（数组退化为指针，完全索引后为元素类型）
        // 4) 常量折叠：仅对非数组的 const 变量且有常量初始化进行折叠

        if (!node.entry)
        {
            // 无效标识符
            errors.emplace_back("Invalid identifier at line " + std::to_string(node.line_num));
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        // 查找符号表项
        auto* attr = symTable.getSymbol(node.entry);
        if (!attr)
        {
            // 未定义标识符
            errors.emplace_back(
                "Undefined identifier '" + node.entry->getName() + "' at line " + std::to_string(node.line_num));
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

        bool   ok     = true;                                     // 语义检查结果
        size_t idxCnt = node.indices ? node.indices->size() : 0;  // 下标数量

        // 访问并检查每个下标表达式
        if (idxCnt > 0)
        {
            // 访问下标表达式并进行类型检查
            for (auto* idxExpr : *node.indices)
            {
                if (!idxExpr) continue;
                ok &= apply(*this, *idxExpr);
                Type* itype = idxExpr->attr.val.value.type;  // 下标表达式类型
                if (!itype || itype->getTypeGroup() == TypeGroup::POINTER || itype->getBaseType() == Type_t::FLOAT ||
                    itype->getBaseType() == Type_t::VOID)
                {
                    // 下标表达式必须为整型
                    errors.emplace_back("Array index must be integer at line " + std::to_string(idxExpr->line_num));
                    ok = false;
                }
            }
        }

        // 数组维度数量检查
        if (idxCnt > attr->arrayDims.size())
        {
            // 如果下标数量超过数组维度数量，报错
            errors.emplace_back(
                "Too many indices for array '" + node.entry->getName() + "' at line " + std::to_string(node.line_num));
            ok = false;
        }

        // 设置类型：未完全索引的数组在表达式中视为指向元素类型的指针
        if (!attr->arrayDims.empty())
        {
            // 数组类型处理
            if (idxCnt == 0) // 未索引，视为指针类型
                node.attr.val.value.type = TypeFactory::getPtrType(attr->type);
            else if (idxCnt < attr->arrayDims.size()) // 部分索引，退化为指针类型
                node.attr.val.value.type = TypeFactory::getPtrType(attr->type);
            else
                node.attr.val.value.type = attr->type;  // 完全索引到元素
        }
        else
        {
            // 非数组类型处理
            if (idxCnt > 0)
            {
                // 非数组类型却有下标访问，报错
                errors.emplace_back("Subscripted value is not an array: '" + node.entry->getName() + "' at line " +
                                    std::to_string(node.line_num));
                ok                        = false;
                node.attr.val.value.type  = voidType;
                node.attr.val.isConstexpr = false;
                return false;
            }
            node.attr.val.value.type = attr->type;
        }

        // 常量折叠：仅对非数组 const 且存在单一编译期初始化值作折叠
        if (attr->isConstDecl && attr->arrayDims.empty() && attr->initList.size() == 1)
        {
            node.attr.val.value       = attr->initList[0];
            node.attr.val.isConstexpr = true;
        }
        else
        {
            node.attr.val.isConstexpr = false;
        }

        return ok;
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
        // (void)node;
        // TODO("Lab3-1: Implement UnaryExpr semantic checking");

        // 访问子表达式并进行类型推断/常量折叠
        bool ok = true;
        if (!node.expr)
        {
            // 无效操作数
            errors.emplace_back("Null operand for unary expression at line " + std::to_string(node.line_num));
            return false;
        }

        ok &= apply(*this, *node.expr); // 访问子表达式并进行类型推断/常量折叠

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
        return ok && !hasError;
    }

    bool ASTChecker::visit(BinaryExpr& node)
    {
        // TODO(Lab3-1): 实现二元表达式的语义检查
        // 访问左右子表达式，检查操作数类型，调用类型推断
        // (void)node;
        // TODO("Lab3-1: Implement BinaryExpr semantic checking");

        // 访问左右子表达式并进行类型推断/常量折叠
        bool ok = true;
        if (!node.lhs || !node.rhs)
        {
            // 无效操作数
            errors.emplace_back("Incomplete binary expression at line " + std::to_string(node.line_num));
            return false;
        }

        // 检查左右操作数
        ok &= apply(*this, *node.lhs);
        ok &= apply(*this, *node.rhs);

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
                ok = false;
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
                    ok = false;
                }
            }
        }

        // 若是赋值，再做一次类型兼容检查（指针与非指针不可互赋）
        if (node.op == Operator::ASSIGN && ok)
        {
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
                    ok = false;
                }
            }
        }

        bool      hasError = false;
        ExprValue result   = typeInfer(node.lhs->attr.val, node.rhs->attr.val, node.op, node, hasError);
        node.attr.op       = node.op;
        node.attr.val      = result;
        return ok && !hasError;
    }

    bool ASTChecker::visit(CallExpr& node)
    {
        // TODO(Lab3-1): 实现函数调用表达式的语义检查
        // 检查函数是否存在，访问实参列表，检查参数数量和类型匹配
        // (void)node;
        // TODO("Lab3-1: Implement CallExpr semantic checking");

        // 函数调用检查：
        // 1) 函数是否存在
        // 2) 逐个访问实参
        // 3) 检查参数数量和类型是否匹配（数组形参退化为指针）

        if (!node.func)
        {
            // 无效函数标识符
            errors.emplace_back("Invalid function call at line " + std::to_string(node.line_num));
            node.attr.val.value.type  = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }

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
        bool   ok       = true;
        size_t argCount = node.args ? node.args->size() : 0;
        if (node.args)
        {
            for (auto* a : *node.args)
            {
                // 访问实参
                if (!a) continue;
                ok &= apply(*this, *a);
            }
        }

        size_t paramCount = decl->params ? decl->params->size() : 0;
        if (argCount != paramCount)
        {
            // 参数数量不匹配
            errors.emplace_back("Argument count mismatch in call to '" + node.func->getName() + "' at line " +
                                std::to_string(node.line_num));
            ok = false;
        }

        // 类型匹配检查
        if (ok && node.args && decl->params)
        {
            for (size_t i = 0; i < paramCount && i < argCount; ++i)
            {
                // 获取形参和实参类型
                auto*      p        = (*decl->params)[i];
                Type*      pty      = p->type;
                const bool isArr    = (p->dims && !p->dims->empty());
                Type*      expected = isArr ? TypeFactory::getPtrType(pty) : pty;
                ExprNode*  argExpr  = (*node.args)[i];
                Type*      actual   = argExpr ? argExpr->attr.val.value.type : voidType;

                if (!expected || !actual)
                {
                    ok = false;
                    errors.emplace_back("Invalid parameter/argument type at line " + std::to_string(node.line_num));
                    continue;
                }

                // 禁止将 void 作为实参传入任何参数
                if (actual->getBaseType() == Type_t::VOID)
                {
                    ok = false;
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
                        ok = false;
                        errors.emplace_back("Argument type mismatch for parameter " + std::to_string(i) +
                                            " in call to '" + node.func->getName() + "' at line " +
                                            std::to_string(node.line_num));
                    }
                }
                else
                {
                    if (actual->getTypeGroup() == TypeGroup::POINTER)
                    {
                        ok = false;
                        errors.emplace_back("Pointer passed to non-pointer parameter " + std::to_string(i) +
                                            " in call to '" + node.func->getName() + "' at line " +
                                            std::to_string(node.line_num));
                    }
                }
            }
        }

        node.attr.val.value.type  = decl->retType;
        node.attr.val.isConstexpr = false;
        return ok;
    }

    bool ASTChecker::visit(CommaExpr& node)
    {
        // TODO(Lab3-1): 实现逗号表达式的语义检查
        // 依序访问各子表达式，结果为最后一个表达式的属性
        // (void)node;
        // TODO("Lab3-1: Implement CommaExpr semantic checking");
        // 逗号表达式：依序访问，结果为最后一个表达式的属性
        if (!node.exprs || node.exprs->empty()) return true;
        bool ok = true;
        for (size_t i = 0; i < node.exprs->size(); ++i)
        {
            ExprNode* e = (*node.exprs)[i];
            if (!e) continue;
            ok &= apply(*this, *e);
            if (i + 1 == node.exprs->size()) node.attr.val = e->attr.val;
        }
        return ok;
    }
}  // namespace FE::AST
