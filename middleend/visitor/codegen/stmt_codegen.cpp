#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    void ASTCodeGen::visit(FE::AST::ExprStmt& node, Module* m)
    {
        if (!node.expr) return;
        apply(*this, *node.expr, m);
    }

    void ASTCodeGen::visit(FE::AST::FuncDeclStmt& node, Module* m)
    {
        if (!node.entry) return;

        DataType     retType = convert(node.retType);
        std::string  funcName(node.entry->getName());
        FuncDefInst* funcDef = new FuncDefInst(retType, funcName);
        Function*    func    = new Function(funcDef);
        m->functions.emplace_back(func);

        enterFunc(func);
        name2reg.enterScope();
        scopeDepth = 0;
        curRetType = retType;
        loopStack.clear();
        reg2attr.clear();

        Block* entryBlock = createBlock();
        entryBlock->setComment(funcName + ".entry");
        enterBlock(entryBlock);
        funcEntryBlock = entryBlock;

        std::vector<std::string> paramTypeStrs;
        if (node.params)
        {
            for (auto* param : *(node.params))
            {
                if (!param || !param->entry) continue;
                bool             isArrayParam = param->dims && !param->dims->empty();
                DataType         paramType    = isArrayParam ? DataType::PTR : convert(param->type);
                std::vector<int> paramDims    = collectArrayDims(param->dims);

                FE::AST::VarAttr attr(param->type, false, scopeDepth);
                attr.arrayDims = paramDims;

                paramTypeStrs.emplace_back(formatIRType(param->type, attr.arrayDims, isArrayParam));

                size_t argReg = getNewRegId();
                funcDef->argRegs.emplace_back(paramType, getRegOperand(argReg));

                if (isArrayParam)
                {
                    name2reg.addSymbol(param->entry, argReg);
                    reg2attr[argReg] = attr;
                }
                else
                {
                    size_t allocaReg = getNewRegId();
                    insertAllocaInst(createAllocaInst(paramType, allocaReg));
                    insert(createStoreInst(paramType, argReg, getRegOperand(allocaReg)));
                    name2reg.addSymbol(param->entry, allocaReg);
                    reg2attr[allocaReg] = attr;
                }
            }
        }
        funcDef->setArgTypeStrs(paramTypeStrs);

        if (node.body) apply(*this, *node.body, m);

        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
        {
            if (curRetType == DataType::VOID)
                insert(createRetInst());
            else if (curRetType == DataType::F32)
                insert(createRetInst(0.0f));
            else
                insert(createRetInst(0));
        }

        name2reg.exitScope();
        scopeDepth = 0;
        reg2attr.clear();
        exitBlock();
        exitFunc();
        curRetType     = DataType::VOID;
        funcEntryBlock = nullptr;
    }

    void ASTCodeGen::visit(FE::AST::VarDeclStmt& node, Module* m)
    {
        if (!node.decl) return;
        if (!curFunc)
        {
            handleGlobalVarDecl(&node, m);
            return;
        }
        apply(*this, *node.decl, m);
    }

    void ASTCodeGen::visit(FE::AST::BlockStmt& node, Module* m)
    {
        name2reg.enterScope();
        ++scopeDepth;
        if (node.stmts)
        {
            for (auto* stmt : *(node.stmts))
            {
                if (!stmt) continue;
                if (!curBlock || (!curBlock->insts.empty() && curBlock->insts.back()->isTerminator()))
                {
                    Block* deadBlock = createBlock();
                    deadBlock->setComment("dead.block");
                    enterBlock(deadBlock);
                }
                apply(*this, *stmt, m);
            }
        }
        name2reg.exitScope();
        --scopeDepth;
    }

    void ASTCodeGen::visit(FE::AST::ReturnStmt& node, Module* m)
    {
        (void)m;
        if (node.retExpr)
        {
            apply(*this, *node.retExpr, m);
            if (curRetType == DataType::VOID)
            {
                insert(createRetInst());
                return;
            }

            size_t   retReg  = getMaxReg();
            DataType retType = convert(node.retExpr->attr.val.value.type);
            retReg           = ensureType(retReg, retType, curRetType);
            insert(createRetInst(curRetType, retReg));
        }
        else
        {
            if (curRetType == DataType::VOID)
                insert(createRetInst());
            else if (curRetType == DataType::F32)
                insert(createRetInst(0.0f));
            else
                insert(createRetInst(0));
        }
    }

    void ASTCodeGen::visit(FE::AST::WhileStmt& node, Module* m)
    {
        Block* condBlock = createBlock();
        condBlock->setComment("while.cond");
        Block* bodyBlock = createBlock();
        bodyBlock->setComment("while.body");
        Block* endBlock = createBlock();
        endBlock->setComment("while.end");

        if (!curBlock || curBlock->insts.empty() || !curBlock->insts.back()->isTerminator())
            insert(createBranchInst(condBlock->blockId));

        enterBlock(condBlock);
        if (node.cond)
        {
            apply(*this, *node.cond, m);
            size_t   condReg  = getMaxReg();
            DataType condType = convert(node.cond->attr.val.value.type);
            condReg           = ensureType(condReg, condType, DataType::I1);
            insert(createBranchInst(condReg, bodyBlock->blockId, endBlock->blockId));
        }
        else
        {
            insert(createBranchInst(bodyBlock->blockId));
        }

        pushLoopContext(condBlock->blockId, endBlock->blockId);

        enterBlock(bodyBlock);
        if (node.body) apply(*this, *node.body, m);
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
            insert(createBranchInst(condBlock->blockId));

        popLoopContext();

        enterBlock(endBlock);
    }

    void ASTCodeGen::visit(FE::AST::IfStmt& node, Module* m)
    {
        Block* thenBlock = createBlock();
        thenBlock->setComment("if.then");
        Block* endBlock = createBlock();
        endBlock->setComment("if.end");
        Block* elseBlock = nullptr;
        if (node.elseStmt)
        {
            elseBlock = createBlock();
            elseBlock->setComment("if.else");
        }

        if (node.cond)
        {
            apply(*this, *node.cond, m);
            size_t   condReg  = getMaxReg();
            DataType condType = convert(node.cond->attr.val.value.type);
            condReg           = ensureType(condReg, condType, DataType::I1);
            if (elseBlock)
                insert(createBranchInst(condReg, thenBlock->blockId, elseBlock->blockId));
            else
                insert(createBranchInst(condReg, thenBlock->blockId, endBlock->blockId));
        }
        else
        {
            insert(createBranchInst(thenBlock->blockId));
        }

        enterBlock(thenBlock);
        if (node.thenStmt) apply(*this, *node.thenStmt, m);
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
            insert(createBranchInst(endBlock->blockId));

        if (elseBlock)
        {
            enterBlock(elseBlock);
            apply(*this, *node.elseStmt, m);
            if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
                insert(createBranchInst(endBlock->blockId));
        }

        enterBlock(endBlock);
    }

    void ASTCodeGen::visit(FE::AST::BreakStmt& node, Module* m)
    {
        (void)node;
        (void)m;
        LoopContext ctx = currentLoopContext();
        if (!curBlock) return;
        if (!curBlock->insts.empty() && curBlock->insts.back()->isTerminator())
        {
            Block* dead = createBlock();
            dead->setComment("break.dead");
            enterBlock(dead);
            return;
        }
        insert(createBranchInst(ctx.breakLabel));
        Block* cont = createBlock();
        cont->setComment("break.cont");
        enterBlock(cont);
    }

    void ASTCodeGen::visit(FE::AST::ContinueStmt& node, Module* m)
    {
        (void)node;
        (void)m;
        LoopContext ctx = currentLoopContext();
        if (!curBlock) return;
        if (!curBlock->insts.empty() && curBlock->insts.back()->isTerminator())
        {
            Block* dead = createBlock();
            dead->setComment("continue.dead");
            enterBlock(dead);
            return;
        }
        insert(createBranchInst(ctx.continueLabel));
        Block* cont = createBlock();
        cont->setComment("continue.cont");
        enterBlock(cont);
    }

    void ASTCodeGen::visit(FE::AST::ForStmt& node, Module* m)
    {
        bool ownScope = node.init && node.init->isVarDeclStmt();
        if (ownScope)
        {
            name2reg.enterScope();
            ++scopeDepth;
        }

        if (node.init) apply(*this, *node.init, m);

        Block* condBlock = createBlock();
        condBlock->setComment("for.cond");
        Block* bodyBlock = createBlock();
        bodyBlock->setComment("for.body");
        Block* stepBlock = createBlock();
        stepBlock->setComment("for.step");
        Block* endBlock = createBlock();
        endBlock->setComment("for.end");

        if (!curBlock || curBlock->insts.empty() || !curBlock->insts.back()->isTerminator())
            insert(createBranchInst(condBlock->blockId));

        enterBlock(condBlock);
        if (node.cond)
        {
            apply(*this, *node.cond, m);
            size_t   condReg  = getMaxReg();
            DataType condType = convert(node.cond->attr.val.value.type);
            condReg           = ensureType(condReg, condType, DataType::I1);
            insert(createBranchInst(condReg, bodyBlock->blockId, endBlock->blockId));
        }
        else
        {
            insert(createBranchInst(bodyBlock->blockId));
        }

        pushLoopContext(stepBlock->blockId, endBlock->blockId);

        enterBlock(bodyBlock);
        if (node.body) apply(*this, *node.body, m);
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
            insert(createBranchInst(stepBlock->blockId));

        enterBlock(stepBlock);
        if (node.step) { apply(*this, *node.step, m); }
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
            insert(createBranchInst(condBlock->blockId));

        popLoopContext();

        enterBlock(endBlock);

        if (ownScope)
        {
            name2reg.exitScope();
            --scopeDepth;
        }
    }
}  // namespace ME
