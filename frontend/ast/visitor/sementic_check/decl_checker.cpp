#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    namespace
    {
        // Cast constant initializer values to the declared type for consistent downstream typing.
        static VarValue castValueToType(const VarValue& value, Type* target)
        {
            if (!target) return value;
            if (target->getTypeGroup() == TypeGroup::POINTER) return value;

            switch (target->getBaseType())
            {
                case Type_t::BOOL: return VarValue(value.getBool());
                case Type_t::INT: return VarValue(value.getInt());
                case Type_t::LL: return VarValue(value.getLL());
                case Type_t::FLOAT: return VarValue(value.getFloat());
                default: return value;
            }
        }
    }  // namespace

    bool ASTChecker::visit(Initializer& node)
    {
        ASSERT(node.init_val && "Null initializer value");
        bool res  = apply(*this, *node.init_val);
        node.attr = node.init_val->attr;
        return res;
    }

    bool ASTChecker::visit(InitializerList& node)
    {
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
        if (!node.lval)
        {
            errors.emplace_back("Invalid variable declarator at line " + std::to_string(node.line_num));
            return false;
        }

        bool success = apply(*this, *node.lval);
        node.attr    = node.lval->attr;

        if (!node.init) return success;

        success &= apply(*this, *node.init);
        Type* varType  = node.lval->attr.val.value.type;
        Type* initType = node.init->attr.val.value.type;
        if (!varType || !initType)
        {
            errors.emplace_back(
                "Cannot determine variable or initializer type at line " + std::to_string(node.line_num));
            return false;
        }
        if (initType->getBaseType() == Type_t::VOID)
        {
            errors.emplace_back(
                "Variable cannot be initialized with void type at line " + std::to_string(node.line_num));
            return false;
        }

        bool lhsPtr = varType->getTypeGroup() == TypeGroup::POINTER;
        bool rhsPtr = initType->getTypeGroup() == TypeGroup::POINTER;
        if (lhsPtr != rhsPtr)
        {
            errors.emplace_back("Initializer type mismatch at line " + std::to_string(node.line_num));
            return false;
        }
        if (lhsPtr && rhsPtr && varType != initType)
        {
            errors.emplace_back("Initializer pointer type mismatch at line " + std::to_string(node.line_num));
            return false;
        }

        return success;
    }

    bool ASTChecker::visit(ParamDeclarator& node)
    {
        bool    success = true;
        VarAttr param(node.type, false, symTable.getScopeDepth());

        if (node.dims && !node.dims->empty())
        {
            param.arrayDims.reserve(node.dims->size());
            for (auto* dimExpr : *(node.dims))
            {
                if (!dimExpr)
                {
                    errors.emplace_back(
                        "Invalid parameter dimension expression at line " + std::to_string(node.line_num));
                    return false;
                }
                success       = apply(*this, *dimExpr) && success;
                Type* dimType = dimExpr->attr.val.value.type;
                if (!dimType)
                {
                    errors.emplace_back(
                        "Cannot determine parameter dimension type at line " + std::to_string(node.line_num));
                    return false;
                }
                if (dimType->getTypeGroup() == TypeGroup::POINTER)
                {
                    errors.emplace_back("Parameter dimension expression cannot be pointer type at line " +
                                        std::to_string(node.line_num));
                    return false;
                }
                if (dimType->getBaseType() != Type_t::INT && dimType->getBaseType() != Type_t::LL &&
                    dimType->getBaseType() != Type_t::BOOL)
                {
                    errors.emplace_back("Parameter dimension expression must be of integer type at line " +
                                        std::to_string(node.line_num));
                    return false;
                }

                param.arrayDims.emplace_back(dimExpr->attr.val.getInt());
            }
        }

        node.attr.val.value.type  = node.type;
        node.attr.val.isConstexpr = false;
        symTable.addSymbol(node.entry, param);
        return success;
    }

    bool ASTChecker::visit(VarDeclaration& node)
    {
        if (!node.decls) return true;
        bool ok = true;

        for (auto* vd : *node.decls)
        {
            if (!vd) continue;
            auto* lval = dynamic_cast<LeftValExpr*>(vd->lval);
            if (!lval || !lval->entry)
            {
                errors.emplace_back("Invalid declarator in declaration at line " + std::to_string(node.line_num));
                ok = false;
                continue;
            }

            if (!symTable.isGlobalScope())
            {
                int      depth = symTable.getScopeDepth();
                VarAttr* exist = symTable.getSymbol(lval->entry);
                if (exist && exist->scopeLevel == depth)
                {
                    errors.emplace_back("Redefinition of variable '" + lval->entry->getName() + "' at line " +
                                        std::to_string(vd->line_num));
                    ok = false;
                    continue;
                }
            }
            else
            {
                VarAttr* exist = symTable.getSymbol(lval->entry);
                if (exist && exist->scopeLevel == -1)
                {
                    errors.emplace_back("Redefinition of global variable '" + lval->entry->getName() + "' at line " +
                                        std::to_string(vd->line_num));
                    ok = false;
                    continue;
                }
            }

            std::vector<int> dims;
            if (lval->indices)
            {
                for (auto* d : *lval->indices)
                {
                    if (!d) continue;
                    ok &= apply(*this, *d);

                    auto* lit = dynamic_cast<LiteralExpr*>(d);
                    if (lit && lit->literal.getInt() == -1)
                    {
                        dims.push_back(-1);
                        continue;
                    }

                    if (!d->attr.val.isConstexpr)
                    {
                        errors.emplace_back(
                            "Array dimension must be integer constant at line " + std::to_string(d->line_num));
                        ok = false;
                        continue;
                    }

                    Type* t = d->attr.val.value.type;
                    if (!t || t->getTypeGroup() == TypeGroup::POINTER || t->getBaseType() == Type_t::VOID)
                    {
                        errors.emplace_back(
                            "Array dimension must be integer constant at line " + std::to_string(d->line_num));
                        ok = false;
                        continue;
                    }

                    dims.push_back(d->attr.val.getInt());
                }
            }

            if (vd->init)
            {
                ok &= apply(*this, *vd->init);
                if (vd->init->singleInit)
                {
                    Type* lhsTy = node.type;
                    Type* rhsTy = vd->init->attr.val.value.type;
                    if (!rhsTy)
                    {
                        int errLine = vd->init ? vd->init->line_num : vd->line_num;
                        errors.emplace_back("Invalid initializer at line " + std::to_string(errLine));
                        ok = false;
                    }
                    else if (rhsTy->getBaseType() == Type_t::VOID)
                    {
                        int errLine = vd->init ? vd->init->line_num : vd->line_num;
                        errors.emplace_back("Initializer cannot be void at line " + std::to_string(errLine));
                        ok = false;
                    }
                    else
                    {
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

            if (!dims.empty() && dims[0] == -1)
            {
                if (auto* il = dynamic_cast<InitializerList*>(vd->init))
                {
                    int inferred = static_cast<int>(il->size());
                    if (inferred <= 0)
                    {
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
                    errors.emplace_back("Array with omitted first dimension requires an initializer list at line " +
                                        std::to_string(vd->line_num));
                    ok = false;
                }
            }

            VarAttr attr(node.type, node.isConstDecl, symTable.getScopeDepth());
            attr.arrayDims = dims;
            if (vd->init && vd->init->singleInit && vd->init->attr.val.isConstexpr)
            {
                attr.initList.clear();
                attr.initList.emplace_back(castValueToType(vd->init->attr.val.value, node.type));
            }
            symTable.addSymbol(lval->entry, attr);
        }

        return ok;
    }

}  // namespace FE::AST
