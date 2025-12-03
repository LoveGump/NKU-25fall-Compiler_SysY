#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(Initializer& node)
    {
        // 示例实现：单个初始化器的语义检查
        // 1) 访问初始化值表达式
        // 2) 将子表达式的属性拷贝到当前节点
        ASSERT(node.init_val && "Null initializer value");
        bool res  = apply(*this, *node.init_val);
        node.attr = node.init_val->attr;
        return res;
    }

    bool ASTChecker::visit(InitializerList& node)
    {
        // 示例实现：初始化器列表的语义检查
        // 遍历列表中的每个初始化器并逐个访问
        if (!node.init_list) return true;
        bool res = true;
        for (auto* init : *(node.init_list))
        {
            // 遍历所有的初始华列表
            if (!init) continue;
            res &= apply(*this, *init);
        }
        return res;
    }

    bool ASTChecker::visit(VarDeclarator& node)
    {
        // TODO(Lab3-1): 实现变量声明器的语义检查
        // 访问左值表达式，同步属性，处理初始化器（如果有）
        auto* lval = dynamic_cast<LeftValExpr*>(node.lval);
        if (!lval || !lval->entry)
        {
            errors.emplace_back("Invalid variable declarator at line " + std::to_string(node.line_num));
            return false;
        }

        bool success = true;
        node.declDims.clear();  // 清空之前的维度信息

        if (lval->indices)
        {  // 数组类型
            for (auto* dimExpr : *lval->indices)
            {
                // 遍历数组下标表达式
                if (!dimExpr) continue;
                success &= apply(*this, *dimExpr);

                auto* lit = dynamic_cast<LiteralExpr*>(dimExpr);
                if (lit && lit->literal.getInt() == -1)
                {
                    // 如果字面量的值为 -1，表示数组维度省略
                    node.declDims.emplace_back(-1);
                    continue;
                }

                if (!dimExpr->attr.val.isConstexpr)
                {
                    // 数组维度必须是编译期常量
                    errors.emplace_back(
                        "Array dimension must be integer constant at line " + std::to_string(dimExpr->line_num));
                    success = false;
                    continue;
                }

                Type* dimType = dimExpr->attr.val.value.type;

                if (!dimType || dimType->getTypeGroup() == TypeGroup::POINTER || dimType->getBaseType() == Type_t::VOID)
                {
                    // 数组下标表达式必须是整数类型
                    errors.emplace_back(
                        "Array dimension must be integer constant at line " + std::to_string(dimExpr->line_num));
                    success = false;
                    continue;
                }
                // 其他类型进行隐式转换为整数类型
                node.declDims.emplace_back(dimExpr->attr.val.getInt());
            }
        }

        Type* declaredType = node.attr.val.value.type;  // 变量声明的类型

        if (node.init)
        {
            // 有初始化列表
            success &= apply(*this, *node.init);  // 检查初始化列表

            if (node.init->singleInit)
            {
                // 单个初始化器
                int   errLine = node.init->line_num;             // 获取错误行号
                Type* rhsTy   = node.init->attr.val.value.type;  // 获取右侧初始化值类型
                if (!declaredType || !rhsTy)
                {
                    // 类型不能为空
                    errors.emplace_back("Invalid initializer at line " + std::to_string(errLine));
                    success = false;
                }
                else if (rhsTy->getBaseType() == Type_t::VOID)
                {
                    // 不能为 void 类型
                    errors.emplace_back("Initializer cannot be void at line " + std::to_string(errLine));
                    success = false;
                }
                else
                {
                    // 检查指针类型匹配
                    bool lhsPtr = declaredType->getTypeGroup() == TypeGroup::POINTER;
                    bool rhsPtr = rhsTy->getTypeGroup() == TypeGroup::POINTER;
                    if (lhsPtr != rhsPtr)
                    {
                        errors.emplace_back("Initializer type mismatch at line " + std::to_string(errLine));
                        success = false;
                    }
                    else if (lhsPtr && rhsPtr && declaredType != rhsTy)
                    {
                        errors.emplace_back("Initializer pointer type mismatch at line " + std::to_string(errLine));
                        success = false;
                    }
                }
            }
        }

        if (!node.declDims.empty() && node.declDims[0] == -1)
        {
            // 如果第一个维度是 -1，尝试从初始化列表推断大小
            if (auto* il = dynamic_cast<InitializerList*>(node.init))
            {
                // 使用初始化列表的大小推断数组维度
                int inferred = static_cast<int>(il->size());
                if (inferred <= 0)
                {
                    // 初始化列表为空，无法推断数组大小
                    errors.emplace_back(
                        "Cannot infer array size from empty initializer list at line " + std::to_string(node.line_num));
                    success = false;
                }
                else
                {
                    // 成功推断数组大小
                    node.declDims[0] = inferred;
                }
            }
            else
            {
                errors.emplace_back("Array with omitted first dimension requires an initializer list at line " +
                                    std::to_string(node.line_num));
                success = false;
            }
        }

        return success;
    }

    bool ASTChecker::visit(ParamDeclarator& node)
    {
        // TODO(Lab3-1): 实现函数形参的语义检查
        // 检查形参重定义，处理数组形参的类型退化，将形参加入符号表
        bool    success = true;
        VarAttr param(node.type, false, symTable.getScopeDepth());  // 构造VarAttr

        if (node.dims && !node.dims->empty())
        {
            // 数组形参，处理维度表达式
            param.arrayDims.reserve(node.dims->size());  // 预留空间
            for (auto* dimExpr : *(node.dims))
            {
                // 遍历每个维度表达式
                if (!dimExpr)
                {
                    // 维度表达式不能为空，为空的已经使用-1占位
                    errors.emplace_back(
                        "Invalid parameter dimension expression at line " + std::to_string(node.line_num));
                    return false;
                }
                success       = apply(*this, *dimExpr) && success;
                Type* dimType = dimExpr->attr.val.value.type;  // 获取维度表达式类型
                if (!dimType)
                {
                    // 类型不能为空
                    errors.emplace_back(
                        "Cannot determine parameter dimension type at line " + std::to_string(node.line_num));
                    return false;
                }
                if (dimType->getTypeGroup() == TypeGroup::POINTER)
                {
                    // 维度表达式不能是指针类型
                    errors.emplace_back("Parameter dimension expression cannot be pointer type at line " +
                                        std::to_string(node.line_num));
                    return false;
                }
                if (dimType->getBaseType() == Type_t::VOID)
                {
                    // 维度表达式不能是void类型
                    errors.emplace_back("Parameter dimension expression must be of integer type at line " +
                                        std::to_string(node.line_num));
                    return false;
                }
                if (!dimExpr->attr.val.isConstexpr)
                {
                    // 维度表达式必须是常量表达式
                    errors.emplace_back(
                        "Parameter dimension expression must be constant at line " + std::to_string(node.line_num));
                    return false;
                }
                // 其他类型进行隐式转换为整数类型

                param.arrayDims.emplace_back(dimExpr->attr.val.getInt());  // 存储维度值
            }
        }

        node.attr.val.value.type  = node.type;
        node.attr.val.isConstexpr = false;

        // addSymbol会检查当前作用域是否已定义
        symTable.addSymbol(node.entry, param);
        return success;
    }

    bool ASTChecker::visit(VarDeclaration& node)
    {
        // TODO(Lab3-1): 实现变量声明的语义检查
        // 遍历声明列表，检查重定义，处理数组维度和初始化，将符号加入符号表

        if (!node.decls) return true;  // 无声明，直接返回
        bool success = true;

        for (auto* vd : *node.decls)
        {
            if (!vd) continue;

            vd->attr.val.value.type  = node.type;
            vd->attr.val.isConstexpr = node.isConstDecl;

            success &= apply(*this, *vd);  // 对数组维度的处理

            auto* lval = dynamic_cast<LeftValExpr*>(vd->lval);  // 左值已经检查过了

            // 检查重定义
            if (!symTable.isGlobalScope())
            {
                // 局部变量，检查当前作用域重定义
                int      depth = symTable.getScopeDepth();
                VarAttr* exist = symTable.getSymbol(lval->entry);
                if (exist && exist->scopeLevel == depth)
                {
                    errors.emplace_back("Redefinition of variable '" + lval->entry->getName() + "' at line " +
                                        std::to_string(vd->line_num));
                    success = false;
                    continue;
                }
            }
            else
            {
                // 全局变量，检查全局重定义
                VarAttr* exist = symTable.getSymbol(lval->entry);
                if (exist && exist->scopeLevel == -1)
                {
                    errors.emplace_back("Redefinition of global variable '" + lval->entry->getName() + "' at line " +
                                        std::to_string(vd->line_num));
                    success = false;
                    continue;
                }
            }

            // 将变量信息加入符号表
            VarAttr attr(node.type, node.isConstDecl, symTable.getScopeDepth());

            attr.arrayDims = vd->declDims;  // 记录数组维度

            if (vd->init && vd->init->singleInit && vd->init->attr.val.isConstexpr)
            {
                // 如果有初始化且是单个常量表达式，记录初始化值
                attr.initList.clear();
                const ExprValue& initExpr = vd->init->attr.val;
                switch (node.type->getBaseType())
                {
                    case Type_t::BOOL: attr.initList.emplace_back(initExpr.getBool()); break;
                    case Type_t::FLOAT: attr.initList.emplace_back(initExpr.getFloat()); break;
                    case Type_t::LL: attr.initList.emplace_back(initExpr.getLL()); break;
                    case Type_t::INT:
                    default: attr.initList.emplace_back(initExpr.getInt()); break;
                }
            }
            symTable.addSymbol(lval->entry, attr);
        }

        return success;
    }

}  // namespace FE::AST
