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
        if (!node.init_list) return true;  // 如果是空列表
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
        // (void)node;
        // TODO("Lab3-1: Implement VarDeclarator semantic checking");
        // 变量声明器：访问左值并处理初始化器
        if (!node.lval)
        {
            errors.emplace_back("Invalid variable declarator at line " + std::to_string(node.line_num));
            return false;
        }
        bool ok = true;
        ok &= apply(*this, *node.lval);

        // 若有初始化器则访问并进行类型兼容检查
        if (node.init)
        {
            // 访问初始化表达式
            ok &= apply(*this, *node.init);

            Type* lhsTy = node.lval->attr.val.value.type; // 左值类型
            Type* rhsTy = node.init->attr.val.value.type; // 初始化表达式类型

            if (!lhsTy || !rhsTy)
            {
                errors.emplace_back("Invalid initializer at line " + std::to_string(node.line_num));
                ok = false;
            }
            else
            {
                // 禁止使用 void 作为初始化表达式
                if (rhsTy->getBaseType() == Type_t::VOID)
                {
                    int errLine = node.init ? node.init->line_num : node.line_num;
                    errors.emplace_back("Initializer cannot be void at line " + std::to_string(errLine));
                    ok = false;
                }
                // 如果左值和初始化表达式类型中有一个是指针类型，则要求另一个也是指针类型
                bool lhsPtr = lhsTy->getTypeGroup() == TypeGroup::POINTER;
                bool rhsPtr = rhsTy->getTypeGroup() == TypeGroup::POINTER;
                if (lhsPtr != rhsPtr)
                {
                    errors.emplace_back("Initializer type mismatch at line " + std::to_string(node.line_num));
                    ok = false;
                }
            }
        }
        // 将左值的属性同步到声明器
        node.attr = node.lval->attr;
        return ok;
    }

    bool ASTChecker::visit(ParamDeclarator& node)
    {
        // TODO(Lab3-1): 实现函数形参的语义检查
        // 检查形参重定义，处理数组形参的类型退化，将形参加入符号表
        // (void)node;
        // TODO("Lab3-1: Implement ParamDeclarator semantic checking");
        // 注意：实际参数加入符号表的操作在函数声明检查中执行
        // 这里仅做维度表达式静态检查（若给出）
        bool ok = true;
        // 如果参数有维度
        if (node.dims)
        {
            // 遍历所有的维度列表
            for (auto* d : *node.dims)
            {
                if (!d) continue;
                ok &= apply(*this, *d); // 检查是否通过
                Type* dt = d->attr.val.value.type; 
                if (!dt || dt->getTypeGroup() == TypeGroup::POINTER || dt->getBaseType() == Type_t::FLOAT)
                {
                    // 数组维度必须为整数
                    errors.emplace_back("Array dimension must be integer at line " + std::to_string(d->line_num));
                    ok = false;
                }
            }
        }
        // 别的属性同步
        node.attr.val.value.type  = node.type;
        node.attr.val.isConstexpr = false;
        return ok;
    }

    bool ASTChecker::visit(VarDeclaration& node)
    {
        // TODO(Lab3-1): 实现变量声明的语义检查
        // 遍历声明列表，检查重定义，处理数组维度和初始化，将符号加入符号表
        // (void)node;
        // TODO("Lab3-1: Implement VarDeclaration semantic checking");
        // 变量声明：遍历声明符，检查重定义与初始化，并加入符号表
        if (!node.decls) return true; // 如果没有声明符，直接返回成功
        bool ok = true; 

        for (auto* vd : *node.decls)
        {
            // 遍历所有的变量声明符
            if (!vd) continue;
            // 先检查左值并获取标识符
            auto* lval = dynamic_cast<LeftValExpr*>(vd->lval);
            if (!lval || !lval->entry)
            {
                // 如果左值无效，记录错误
                errors.emplace_back("Invalid declarator in declaration at line " + std::to_string(node.line_num));
                ok = false;
                continue;
            }

            // 重定义检查（当前作用域）
            if (!symTable.isGlobalScope())
            {
                // 局部作用域：只需检查当前层是否已有该符号
                int      depth = symTable.getScopeDepth();
                VarAttr* exist = symTable.getSymbol(lval->entry);
                if (exist && exist->scopeLevel == depth)
                {
                    // 如果已经存在于当前作用域，报重定义错误
                    errors.emplace_back("Redefinition of variable '" + lval->entry->getName() + "' at line " +
                                        std::to_string(vd->line_num));
                    ok = false;
                    continue;
                }
            }
            else
            {
                // 全局作用域：检查 globalSymbols 中是否存在（通过 getSymbol 找到且 scopeLevel==-1）
                VarAttr* exist = symTable.getSymbol(lval->entry);
                if (exist && exist->scopeLevel == -1)
                {
                    // 如果已经存在于全局作用域，报重定义错误
                    errors.emplace_back("Redefinition of global variable '" + lval->entry->getName() + "' at line " +
                                        std::to_string(vd->line_num));
                    ok = false;
                    continue;
                }
            }

            // 维度表达式检查：如果 LeftValExpr 携带 indices，则这些来自语法构造的维度表达式
            std::vector<int> dims;
            if (lval->indices)
            {
                // 遍历所有的索引
                for (auto* d : *lval->indices)
                {
                    if (!d) continue;
                    ok &= apply(*this, *d);
                    // 维度允许省略首维（以 -1 表示），其余维度需为非负整数常量
                    Type* t = d->attr.val.value.type;
                    if (t && t->getBaseType() == Type_t::INT)
                    {
                        // 必须为int
                        int v = d->attr.val.getInt();
                        dims.push_back(v);
                    }
                    else
                    {
                        // 如果是字面量-1（首维省略），也允许
                        auto* lit = dynamic_cast<LiteralExpr*>(d);
                        if (lit && lit->literal.getInt() == -1) { dims.push_back(-1); }
                        else
                        {
                            errors.emplace_back(
                                "Array dimension must be integer constant at line " + std::to_string(d->line_num));
                            ok = false;
                        }
                    }
                }
            }

            // 访问初始化语句 并进行基本类型兼容检查
            if (vd->init)
            {
                ok &= apply(*this, *vd->init);
                // 对单值初始化执行类型兼容检查：禁止 void，指针/非指针不匹配
                if (vd->init->singleInit)
                {
                    Type* lhsTy = node.type; // 左值类型
                    Type* rhsTy = vd->init->attr.val.value.type; // 初始化表达式类型
                    if (!rhsTy)
                    {
                        // 右值类型无效，记录错误
                        int errLine = vd->init ? vd->init->line_num : vd->line_num;
                        errors.emplace_back("Invalid initializer at line " + std::to_string(errLine));
                        ok = false;
                    }
                    else if (rhsTy->getBaseType() == Type_t::VOID)
                    {
                        // 禁止使用 void 作为初始化表达式
                        int errLine = vd->init ? vd->init->line_num : vd->line_num;
                        errors.emplace_back("Initializer cannot be void at line " + std::to_string(errLine));
                        ok = false;
                    }
                    else
                    {
                        // 如果左值和初始化表达式类型中有一个是指针类型，则要求另一个也是指针类型
                        bool lhsPtr = lhsTy->getTypeGroup() == TypeGroup::POINTER;
                        bool rhsPtr = rhsTy->getTypeGroup() == TypeGroup::POINTER;
                        if (lhsPtr != rhsPtr)
                        {
                            int errLine = vd->init ? vd->init->line_num : vd->line_num;
                            errors.emplace_back("Initializer type mismatch at line " + std::to_string(errLine));
                            ok = false;
                        }
                    }
                }
            }

            // 若数组首维为省略（-1），尝试通过初始化列表推断大小
            if (!dims.empty() && dims[0] == -1)
            {
                // 首维省略，尝试推断
                if (auto* il = dynamic_cast<InitializerList*>(vd->init))
                {
                    // 使用最外层初始化项个数作为首维大小
                    int inferred = static_cast<int>(il->size());
                    if (inferred <= 0)
                    {
                        // 无法从空初始化列表推断大小
                        errors.emplace_back("Cannot infer array size from empty initializer list at line " +
                                            std::to_string(vd->line_num));
                        ok = false;
                    }
                    else
                    {
                        dims[0] = inferred;
                    }
                }
                else
                {
                    // 没有初始化列表，无法推断数组大小
                    errors.emplace_back("Array with omitted first dimension requires an initializer list at line " +
                                        std::to_string(vd->line_num));
                    ok = false;
                }
            }

            // 构造符号属性并加入符号表
            VarAttr attr(node.type, node.isConstDecl, symTable.getScopeDepth());
            attr.arrayDims = dims;
            if (vd->init && vd->init->singleInit)
            {
                // 单值初始化：记录初值，用于 const 折叠
                attr.initList.clear();
                attr.initList.emplace_back(vd->init->attr.val.value);
            }
            symTable.addSymbol(lval->entry, attr);
        }

        return ok;
    }
}  // namespace FE::AST
