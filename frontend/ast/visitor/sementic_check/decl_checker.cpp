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
        if (!node.lval)
        {
            // 左值表达式不能为空
            errors.emplace_back("Invalid variable declarator at line " + std::to_string(node.line_num));
            return false;
        }

        bool success = apply(*this, *node.lval);
        node.attr    = node.lval->attr;  // 同步属性

        if (!node.init) return success;  // 无初始化器，直接返回

        // 处理初始化器
        success &= apply(*this, *node.init);
        Type* varType  = node.lval->attr.val.value.type;  // 变量类型
        Type* initType = node.init->attr.val.value.type;  // 初始化器类型
        if (!varType || !initType)
        {
            // 类型不能为空
            errors.emplace_back(
                "Cannot determine variable or initializer type at line " + std::to_string(node.line_num));
            return false;
        }
        if (initType->getBaseType() == Type_t::VOID)
        {
            // 变量不能用void类型初始化
            errors.emplace_back(
                "Variable cannot be initialized with void type at line " + std::to_string(node.line_num));
            return false;
        }

        // 检查指针与非指针类型的匹配
        bool lhsPtr = varType->getTypeGroup() == TypeGroup::POINTER;
        bool rhsPtr = initType->getTypeGroup() == TypeGroup::POINTER;
        if (lhsPtr != rhsPtr)
        {
            errors.emplace_back("Initializer type mismatch at line " + std::to_string(node.line_num));
            return false;
        }
        if (lhsPtr && rhsPtr && varType != initType)
        {
            // 指针类型还需进一步检查是否完全相同
            errors.emplace_back("Initializer pointer type mismatch at line " + std::to_string(node.line_num));
            return false;
        }
        // 基本类型可以进行隐式类型转换

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
            // 遍历每个变量声明器
            if (!vd) continue;
            auto* lval = dynamic_cast<LeftValExpr*>(vd->lval);
            if (!lval || !lval->entry)
            {
                // 左值表达式和符号表项不能为空
                errors.emplace_back("Invalid declarator in declaration at line " + std::to_string(node.line_num));
                success = false;
                continue;
            }

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

            // 数组的维度表达式
            std::vector<int> dims;
            if (lval->indices)
            {
                // 如果左值是数组，记录对应的数组下标
                for (auto* d : *lval->indices)
                {
                    if (!d) continue;
                    success &= apply(*this, *d);  // 访问下标表达式

                    auto* lit = dynamic_cast<LiteralExpr*>(d);
                    if (lit && lit->literal.getInt() == -1)
                    {
                        // 如果为空 第一个是-1占位符
                        dims.push_back(-1);
                        continue;
                    }

                    if (!d->attr.val.isConstexpr)
                    {
                        // 如果不是编译期常量，报错
                        errors.emplace_back(
                            "Array dimension must be integer constant at line " + std::to_string(d->line_num));
                        success = false;
                        continue;
                    }

                    // 获取对应的类型
                    Type* t = d->attr.val.value.type;
                    if (!t || t->getTypeGroup() == TypeGroup::POINTER || t->getBaseType() == Type_t::VOID)
                    {
                        // 下标不能为void 或者 指针
                        errors.emplace_back(
                            "Array dimension must be integer constant at line " + std::to_string(d->line_num));
                        success = false;
                        continue;
                    }
                    // 其他类型转换
                    dims.push_back(d->attr.val.getInt());
                }
            }

            // 如果存在对应的初始化器
            if (vd->init)
            {
                success &= apply(*this, *vd->init);  // 访问初始化器
                if (vd->init->singleInit)
                {
                    // 如果是单个初始化表达式，检查类型匹配
                    Type* lhsTy = node.type;
                    Type* rhsTy = vd->init->attr.val.value.type;
                    if (!rhsTy)
                    {
                        // 如果初始化器类型无效
                        int errLine = vd->init ? vd->init->line_num : vd->line_num;
                        errors.emplace_back("Invalid initializer at line " + std::to_string(errLine));
                        success = false;
                    }
                    else if (rhsTy->getBaseType() == Type_t::VOID)
                    {
                        // 如果初始化器类型为void，报错
                        int errLine = vd->init ? vd->init->line_num : vd->line_num;
                        errors.emplace_back("Initializer cannot be void at line " + std::to_string(errLine));
                        success = false;
                    }
                    else
                    {
                        // 如果初始化器类型为指针与左值类型不匹配，报错
                        bool lhsPtr = lhsTy->getTypeGroup() == TypeGroup::POINTER;
                        bool rhsPtr = rhsTy->getTypeGroup() == TypeGroup::POINTER;
                        if (lhsPtr != rhsPtr)
                        {
                            int errLine = vd->init ? vd->init->line_num : vd->line_num;
                            errors.emplace_back("Initializer type mismatch at line " + std::to_string(errLine));
                            success = false;
                        }
                    }
                }
            }

            // 处理数组维度推导
            if (!dims.empty() && dims[0] == -1)
            {
                if (auto* il = dynamic_cast<InitializerList*>(vd->init))
                {
                    int inferred = static_cast<int>(il->size());
                    if (inferred <= 0)
                    {
                        errors.emplace_back("Cannot infer array size from empty initializer list at line " +
                                            std::to_string(vd->line_num));
                        success = false;
                    }
                    else
                    {
                        // 推导出第一个维度大小 至少为1
                        dims[0] = inferred;
                    }
                }
                else
                {
                    // 非初始化列表无法推导出第一个维度大小 报错
                    errors.emplace_back("Array with omitted first dimension requires an initializer list at line " +
                                        std::to_string(vd->line_num));
                    success = false;
                }
            }

            // 将变量加入符号表
            VarAttr attr(node.type, node.isConstDecl, symTable.getScopeDepth());
            attr.arrayDims = dims;
            if (vd->init && vd->init->singleInit && vd->init->attr.val.isConstexpr)
            {
                attr.initList.clear();
                const ExprValue& initExpr = vd->init->attr.val;
                switch (node.type ? node.type->getBaseType() : Type_t::INT)
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
