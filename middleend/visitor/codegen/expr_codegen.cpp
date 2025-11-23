#include <middleend/visitor/codegen/ast_codegen.h>
#include <sstream>

namespace ME
{
    // 把左值表达式转换为地址操作数
    Operand* ASTCodeGen::ensureLValueAddress(FE::AST::LeftValExpr& node, Module* m, size_t extraZeros)
    {
        (void)m;
        auto* entry = node.entry;  // 获取符号表项
        ASSERT(entry && "Left value without symbol entry");
        const FE::AST::VarAttr* attr = getVarAttr(entry);  // 获取变量属性
        ASSERT(attr && "Symbol attribute missing during codegen");

        Operand* basePtr = nullptr;
        if (attr->scopeLevel == -1) { basePtr = getGlobalOperand(entry->getName()); }
        else
        {
            size_t reg = name2reg.getReg(entry);
            ASSERT(reg != static_cast<size_t>(-1) && "Local symbol without register binding");
            basePtr = getRegOperand(reg);
        }

        std::vector<int> filteredDims;
        filteredDims.reserve(attr->arrayDims.size());
        for (int dim : attr->arrayDims)
        {
            if (dim <= 0) continue;
            filteredDims.push_back(dim);
        }

        std::vector<Operand*> idxOps;
        bool                  needLeadingZero = !attr->arrayDims.empty() && attr->arrayDims.front() > 0;
        if (needLeadingZero) idxOps.push_back(getImmeI32Operand(0));

        if (node.indices)
        {
            for (auto* idxNode : *(node.indices))
            {
                if (!idxNode) continue;
                apply(*this, *idxNode, m);
                size_t   idxReg  = getMaxReg();
                DataType idxType = convert(idxNode->attr.val.value.type);
                idxReg           = ensureType(idxReg, idxType, DataType::I32);
                idxOps.push_back(getRegOperand(idxReg));
            }
        }

        while (extraZeros > 0)
        {
            idxOps.push_back(getImmeI32Operand(0));
            --extraZeros;
        }

        Operand* ptr = basePtr;
        if (!idxOps.empty())
        {
            size_t   resReg = getNewRegId();
            GEPInst* gep    = createGEP_I32Inst(convert(attr->type), basePtr, filteredDims, idxOps, resReg);
            insert(gep);
            ptr = getRegOperand(resReg);
        }

        return ptr;
    }

    void ASTCodeGen::visit(FE::AST::LeftValExpr& node, Module* m)
    {
        // TODO(Lab 3-2): 生成左值表达式的取址/取值 IR
        // 查找变量位置（全局或局部），处理数组下标/GEP，必要时发出load

        // 获取地址操作数
        Operand* ptr = ensureLValueAddress(node, m);

        auto* type = node.attr.val.value.type;                            // 获取变量类型
        if (!type) return;                                                // 类型缺失时直接返回
        if (type->getTypeGroup() == FE::AST::TypeGroup::POINTER) return;  // 指针类型直接返回地址

        size_t   resReg   = getNewRegId();
        DataType loadType = convert(type);
        insert(createLoadInst(loadType, ptr, resReg));
    }

    void ASTCodeGen::visit(FE::AST::LiteralExpr& node, Module* m)
    {
        (void)m;

        size_t reg = getNewRegId();
        switch (node.literal.type->getBaseType())
        {
            case FE::AST::Type_t::INT:
            case FE::AST::Type_t::LL:  // treat as I32
            {
                int             val  = node.literal.getInt();
                ArithmeticInst* inst = createArithmeticI32Inst_ImmeAll(Operator::ADD, val, 0, reg);  // reg = val + 0
                insert(inst);
                break;
            }
            case FE::AST::Type_t::FLOAT:
            {
                float           val  = node.literal.getFloat();
                ArithmeticInst* inst = createArithmeticF32Inst_ImmeAll(Operator::FADD, val, 0, reg);  // reg = val + 0
                insert(inst);
                break;
            }
            default: ERROR("Unsupported literal type");
        }
    }

    void ASTCodeGen::visit(FE::AST::UnaryExpr& node, Module* m)
    {
        if (!node.expr) return;
        handleUnaryCalc(*node.expr, node.op, curBlock, m);
    }

    void ASTCodeGen::handleAssign(FE::AST::LeftValExpr& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        Operand* ptr = ensureLValueAddress(lhs, m);
        apply(*this, rhs, m);

        size_t   rhsReg  = getMaxReg();
        DataType rhsType = convert(rhs.attr.val.value.type);

        DataType lhsType = convert(lhs.attr.val.value.type);
        rhsReg           = ensureType(rhsReg, rhsType, lhsType);

        insert(createStoreInst(lhsType, rhsReg, ptr));
    }
    void ASTCodeGen::handleLogicalAnd(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        (void)node;
        apply(*this, lhs, m);
        size_t   lhsReg  = getMaxReg();
        DataType lhsType = convert(lhs.attr.val.value.type);
        lhsReg           = ensureType(lhsReg, lhsType, DataType::I1);

        Block* lhsBlock = curBlock;

        Block* rhsEntryBlock = createBlock();
        rhsEntryBlock->setComment("and.rhs");
        Block* endBlock = createBlock();
        endBlock->setComment("and.end");

        insert(createBranchInst(lhsReg, rhsEntryBlock->blockId, endBlock->blockId));

        enterBlock(rhsEntryBlock);
        apply(*this, rhs, m);
        size_t   rhsReg  = getMaxReg();
        DataType rhsType = convert(rhs.attr.val.value.type);
        rhsReg           = ensureType(rhsReg, rhsType, DataType::I1);

        Block* rhsExitBlock = curBlock;
        if (rhsExitBlock && (rhsExitBlock->insts.empty() || !rhsExitBlock->insts.back()->isTerminator()))
            insert(createBranchInst(endBlock->blockId));

        enterBlock(endBlock);
        size_t   resultReg = getNewRegId();
        PhiInst* phi       = new PhiInst(DataType::I1, getRegOperand(resultReg));
        phi->addIncoming(getRegOperand(lhsReg), getLabelOperand(lhsBlock->blockId));
        phi->addIncoming(getRegOperand(rhsReg), getLabelOperand(rhsExitBlock->blockId));
        insert(phi);
    }
    void ASTCodeGen::handleLogicalOr(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        (void)node;
        apply(*this, lhs, m);
        size_t   lhsReg  = getMaxReg();
        DataType lhsType = convert(lhs.attr.val.value.type);
        lhsReg           = ensureType(lhsReg, lhsType, DataType::I1);

        Block* lhsBlock = curBlock;

        Block* rhsEntryBlock = createBlock();
        rhsEntryBlock->setComment("or.rhs");
        Block* endBlock = createBlock();
        endBlock->setComment("or.end");

        insert(createBranchInst(lhsReg, endBlock->blockId, rhsEntryBlock->blockId));

        enterBlock(rhsEntryBlock);
        apply(*this, rhs, m);
        size_t   rhsReg  = getMaxReg();
        DataType rhsType = convert(rhs.attr.val.value.type);
        rhsReg           = ensureType(rhsReg, rhsType, DataType::I1);

        Block* rhsExitBlock = curBlock;
        if (rhsExitBlock && (rhsExitBlock->insts.empty() || !rhsExitBlock->insts.back()->isTerminator()))
            insert(createBranchInst(endBlock->blockId));

        enterBlock(endBlock);
        size_t   resultReg = getNewRegId();
        PhiInst* phi       = new PhiInst(DataType::I1, getRegOperand(resultReg));
        phi->addIncoming(getRegOperand(lhsReg), getLabelOperand(lhsBlock->blockId));
        phi->addIncoming(getRegOperand(rhsReg), getLabelOperand(rhsExitBlock->blockId));
        insert(phi);
    }
    void ASTCodeGen::visit(FE::AST::BinaryExpr& node, Module* m)
    {
        switch (node.op)
        {
            case FE::AST::Operator::ASSIGN:
            {
                auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(node.lhs);
                ASSERT(lval && "Assignment lhs is not lvalue");
                handleAssign(*lval, *node.rhs, m);
                break;
            }
            case FE::AST::Operator::AND: handleLogicalAnd(node, *node.lhs, *node.rhs, m); break;
            case FE::AST::Operator::OR: handleLogicalOr(node, *node.lhs, *node.rhs, m); break;
            default: handleBinaryCalc(*node.lhs, *node.rhs, node.op, curBlock, m); break;
        }
    }

    void ASTCodeGen::visit(FE::AST::CallExpr& node, Module* m)
    {
        (void)m;
        std::string funcName = node.func ? node.func->getName() : "";

        FE::AST::FuncDeclStmt* decl = nullptr;
        if (node.func)
        {
            auto it = funcDecls.find(node.func);
            if (it != funcDecls.end()) decl = it->second;
        }

        size_t                   argCount = node.args ? node.args->size() : 0;
        CallInst::argList        args;
        std::vector<std::string> argTypeStrs;
        argTypeStrs.reserve(argCount);
        for (size_t i = 0; i < argCount; ++i)
        {
            FE::AST::ExprNode* argNode = (*node.args)[i];
            if (!argNode) continue;

            apply(*this, *argNode, m);

            FE::AST::Type*   expectedType = nullptr;
            bool             expectPtr    = false;
            std::vector<int> expectedDims;
            if (decl && decl->params && i < decl->params->size())
            {
                auto* paramDecl = (*decl->params)[i];
                if (paramDecl)
                {
                    expectedType = paramDecl->type;
                    expectPtr    = paramDecl->dims && !paramDecl->dims->empty();
                    expectedDims = collectArrayDims(paramDecl->dims);
                }
            }
            size_t expectedParamDims = 0;
            for (int dim : expectedDims)
                if (dim > 0) ++expectedParamDims;

            FE::AST::Type* argType = argNode->attr.val.value.type;
            DataType       argDT   = convert(argType);

            bool                    isPtrArg   = argType && argType->getTypeGroup() == FE::AST::TypeGroup::POINTER;
            const FE::AST::VarAttr* argAttr    = nullptr;
            size_t                  usedIdx    = 0;
            size_t                  actualDims = 0;
            size_t                  extraZeros = 0;
            std::vector<int>        attrDims;
            if (isPtrArg)
            {
                Operand* op = nullptr;
                if (auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(argNode))
                {
                    argAttr = getVarAttr(lval->entry);
                    usedIdx = lval->indices ? lval->indices->size() : 0;
                    if (argAttr)
                    {
                        for (int dim : argAttr->arrayDims)
                        {
                            if (dim <= 0) continue;
                            attrDims.push_back(dim);
                            ++actualDims;
                        }
                        if (actualDims > expectedParamDims + usedIdx)
                            extraZeros = actualDims - expectedParamDims - usedIdx;
                    }
                    op = ensureLValueAddress(*lval, m, extraZeros);
                }
                else
                {
                    op = getRegOperand(getMaxReg());
                }
                args.emplace_back(DataType::PTR, op);
            }
            else
            {
                size_t   argReg   = getMaxReg();
                DataType expectDT = expectedType ? convert(expectedType) : argDT;
                argReg            = ensureType(argReg, argDT, expectDT);
                args.emplace_back(expectDT, getRegOperand(argReg));
            }

            FE::AST::Type*   typeForStr = expectedType ? expectedType : argType;
            bool             forcePtr   = expectPtr || isPtrArg;
            std::string      argTypeStr;
            size_t           consumedDims = std::min(actualDims, usedIdx + extraZeros);
            std::vector<int> remainingDims;
            if (consumedDims < attrDims.size()) remainingDims.assign(attrDims.begin() + consumedDims, attrDims.end());

            if (typeForStr)
            {
                std::vector<int> typeDims;
                if (!expectedDims.empty())
                    typeDims = expectedDims;
                else if (!remainingDims.empty())
                    typeDims = remainingDims;
                argTypeStr = formatIRType(typeForStr, typeDims, forcePtr);
            }
            else
            {
                std::stringstream ss;
                ss << args.back().first;
                argTypeStr = ss.str();
                if (forcePtr) argTypeStr += "*";
            }
            argTypeStrs.emplace_back(argTypeStr);
        }

        DataType retType = convert(node.attr.val.value.type);
        if (retType == DataType::UNK && decl) retType = convert(decl->retType);

        if (retType == DataType::VOID)
        {
            CallInst* call = createCallInst(retType, funcName, args);
            call->setArgTypeStrs(argTypeStrs);
            insert(call);
        }
        else
        {
            size_t    resReg = getNewRegId();
            CallInst* call   = createCallInst(retType, funcName, args, resReg);
            call->setArgTypeStrs(argTypeStrs);
            insert(call);
        }
    }

    void ASTCodeGen::visit(FE::AST::CommaExpr& node, Module* m)
    {
        if (!node.exprs) return;
        for (auto* expr : *(node.exprs))
        {
            if (!expr) continue;
            apply(*this, *expr, m);
        }
    }
}  // namespace ME
