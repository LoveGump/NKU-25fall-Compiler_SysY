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
        // TODO(Lab 3-2): 生成函数定义 IR（形参、入口/结束基本块、返回补丁）
        // 设置函数返回类型与参数寄存器，创建基本块骨架，并生成函数体
        FuncDefInst* funcDef = new FuncDefInst(convert(node.retType), node.entry->getName());  // 创建函数定义指令
        Function*    func    = new Function(funcDef);                                          // 创建函数对象
        m->functions.emplace_back(func);  // 将函数添加到模块中

        enterFunc(func);        // 进入函数
        name2reg.enterScope();  // 进入新作用域
        // needtodo
        scopeDepth = 0;     // 重置作用域深度
        loopStack.clear();  // 清空循环栈
        reg2attr.clear();   // 清空寄存器到变量属性的映射

        Block* entryBlock = createBlock();                         // 创建入口基本块
        entryBlock->setComment(node.entry->getName() + ".entry");  // 设置基本块注释
        enterBlock(entryBlock);                                    // 进入入口基本块
        funcEntryBlock = entryBlock;                               // 记录入口基本块

        if (node.params)
        {
            // 处理函数参数
            for (auto* param : *(node.params))
            {
                if (!param || !param->entry) continue;
                // 判断参数是否为数组类型
                bool isArrayParam = param->dims && !param->dims->empty();
                // 获取参数类型与维度
                DataType paramType = isArrayParam ? DataType::PTR : convert(param->type);

                std::vector<int> paramDims;
                if (param->dims)
                {
                    // 数组参数，收集维度信息
                    for (auto* expr : *(param->dims))
                    {
                        // -1 表示不定长数组
                        paramDims.push_back(expr->attr.val.getInt());
                    }
                }

                // 储存参数变量属性
                FE::AST::VarAttr attr(param->type, false, scopeDepth);
                attr.arrayDims = paramDims;  // 设置数组维度

                // 为参数分配寄存器
                size_t argReg = getNewRegId();  // 获取新的寄存器编号
                funcDef->argRegs.emplace_back(
                    paramType, getRegOperand(argReg));  // 根据寄存器编号创建操作数并添加到函数定义

                if (isArrayParam)
                {
                    // 数组参数作为指针处理，直接加入符号表与变量属性表
                    name2reg.addSymbol(param->entry, argReg);
                    reg2attr[argReg] = attr;
                }
                else
                {
                    // 非参数组参数 需要在函数体内分配局部变量并存储参数值
                    size_t allocaReg = getNewRegId();
                    // 在块的开头插入alloca指令，在栈上分配空间
                    insertAllocaInst(createAllocaInst(paramType, allocaReg));
                    insert(createStoreInst(paramType, argReg, getRegOperand(allocaReg)));  // 存储参数值到分配的空间
                    // 加入符号表与变量属性表
                    name2reg.addSymbol(param->entry, allocaReg);
                    reg2attr[allocaReg] = attr;
                }
            }
        }

        // 为函数体生成 IR
        if (node.body) { apply(*this, *node.body, m); }

        // 处理函数结束时的返回指令补丁
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
        {
            // 如果当前基本块没有终止指令，插入默认返回指令
            if (curFunc->funcDef->retType == DataType::VOID) { insert(createRetInst()); }
            else if (curFunc->funcDef->retType == DataType::F32) { insert(createRetInst(0.0f)); }
            else { insert(createRetInst(0)); }
        }

        // 清理函数生成上下文
        name2reg.exitScope();
        scopeDepth = -1;
        reg2attr.clear();
        exitBlock();
        exitFunc();
        funcEntryBlock = nullptr;
    }

    void ASTCodeGen::visit(FE::AST::VarDeclStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成变量声明语句 IR（局部变量分配、初始化）
        if (!node.decl) return;
        apply(*this, *node.decl, m);
    }

    void ASTCodeGen::visit(FE::AST::BlockStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成语句块 IR（作用域管理，顺序生成子语句）
        name2reg.enterScope();
        ++scopeDepth;
        if (node.stmts)
        {
            for (auto* stmt : *(node.stmts))
            {
                // 遍历子语句列表
                if (!stmt) continue;
                apply(*this, *stmt, m);
            }
        }
        name2reg.exitScope();
        --scopeDepth;
    }

    void ASTCodeGen::visit(FE::AST::ReturnStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 return 语句 IR（可选返回值与类型转换）
        (void)m;
        if (node.retExpr)
        {
            // 先创建返回值表达式的 IR
            apply(*this, *node.retExpr, m);
            // 处理返回值类型转换与返回指令生成
            if (curFunc->funcDef->retType == DataType::VOID)
            {
                // 函数返回类型为 void，直接生成无返回值的 return 指令
                insert(createRetInst());
            }
            else
            {
                size_t   retReg  = getMaxReg();  // 获取返回值寄存器
                DataType retType = convert(node.retExpr->attr.val.value.type);

                auto insts = createTypeConvertInst(retType, curFunc->funcDef->retType, retReg);
                if (!insts.empty())
                {
                    retReg = getMaxReg();  // 获取新的寄存器值
                    for (auto* inst : insts) { insert(inst); }
                }
                insert(createRetInst(curFunc->funcDef->retType, retReg));
            }
        }
        else
        {
            // 如果没有返回值表达式，生成默认返回指令
            if (curFunc->funcDef->retType == DataType::VOID) { insert(createRetInst()); }
            else if (curFunc->funcDef->retType == DataType::F32) { insert(createRetInst(0.0f)); }
            else { insert(createRetInst(0)); }
        }

        // 创建死块，防止返回后继续执行
        Block* deadBlock = createBlock();
        deadBlock->setComment("return.dead");
        enterBlock(deadBlock);
    }

    void ASTCodeGen::visit(FE::AST::WhileStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 while 循环 IR（条件块、循环体与结束块、循环签）
        Block* condBlock = createBlock();
        condBlock->setComment("while.cond");
        Block* bodyBlock = createBlock();
        bodyBlock->setComment("while.body");
        Block* endBlock = createBlock();
        endBlock->setComment("while.end");

        if (!curBlock || curBlock->insts.empty() || !curBlock->insts.back()->isTerminator())
        {
            // 如果当前块没有终止指令，插入跳转到条件块
            insert(createBranchInst(condBlock->blockId));
        }
        enterBlock(condBlock);  // 进入条件块

        // 生成条件表达式的 IR
        apply(*this, *node.cond, m);
        // 将条件值转换为布尔类型，并生成条件分支指令
        size_t   condReg  = getMaxReg();
        DataType condType = convert(node.cond->attr.val.value.type);
        auto     insts    = createTypeConvertInst(condType, DataType::I1, condReg);
        // 插入转换指令
        if (!insts.empty())
        {
            condReg = getMaxReg();
            for (auto* inst : insts) { insert(inst); }
        }
        insert(createBranchInst(condReg, bodyBlock->blockId, endBlock->blockId));

        // 设置循环上下文标签，continue 指向条件块，break 指向结束块，每次循环都创建新的上下文
        pushLoopContext(condBlock->blockId, endBlock->blockId);

        enterBlock(bodyBlock);  // 进入循环体块
        // 生成循环体语句的 IR
        if (node.body) { apply(*this, *node.body, m); }

        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
        {
            // 如果循环体块没有终止指令，插入跳转回条件块指令
            insert(createBranchInst(condBlock->blockId));
        }
        // 弹出循环上下文标签
        popLoopContext();

        // 跳出循环，进入结束块
        enterBlock(endBlock);
    }

    void ASTCodeGen::visit(FE::AST::IfStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 if/else IR（then/else/end 基本块与条件分支）
        Block* thenBlock = createBlock();
        thenBlock->setComment("if.then");
        Block* endBlock = createBlock();
        endBlock->setComment("if.end");
        Block* elseBlock = nullptr;
        if (node.elseStmt)
        {
            // 如果有 else 分支，创建 else 基本块
            elseBlock = createBlock();
            elseBlock->setComment("if.else");
        }

        // 生成条件表达式的 IR
        apply(*this, *node.cond, m);
        size_t   condReg  = getMaxReg();
        DataType condType = convert(node.cond->attr.val.value.type);
        auto     insts    = createTypeConvertInst(condType, DataType::I1, condReg);
        // 插入转换指令
        if (!insts.empty())
        {
            condReg = getMaxReg();
            for (auto* inst : insts) { insert(inst); }
        }
        if (elseBlock)
        {
            // 有 else 分支，生成条件分支到 then 或 else 块
            insert(createBranchInst(condReg, thenBlock->blockId, elseBlock->blockId));
        }
        else
        {
            // 没有 else 分支，生成条件分支到 then 或 end 块
            insert(createBranchInst(condReg, thenBlock->blockId, endBlock->blockId));
        }

        // 生成 then 块与可选的 else 块
        enterBlock(thenBlock);
        // 生成 then 块的 IR
        if (node.thenStmt) { apply(*this, *node.thenStmt, m); }
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
        {
            // then 块没有终止指令，跳转到 end 块
            insert(createBranchInst(endBlock->blockId));
        }
        // 生成 else 块（如果存在）
        if (elseBlock)
        {
            enterBlock(elseBlock);
            apply(*this, *node.elseStmt, m);
            if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
            {  // else 块没有终止指令，跳转到 end 块
                insert(createBranchInst(endBlock->blockId));
            }
        }
        // 进入 if 语句的结束块
        enterBlock(endBlock);
    }

    void ASTCodeGen::visit(FE::AST::BreakStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 break 的无条件跳转至循环结束块
        (void)node;
        (void)m;
        // 获取当前循环上下文标签
        LoopContext ctx = currentLoopContext();
        if (!curBlock) return;  // 如果不在基本块内直接返回
        if (!curBlock->insts.empty() && curBlock->insts.back()->isTerminator())
        {
            // 当前基本块已终止，创建死代码块以保持结构完整
            Block* dead = createBlock();
            dead->setComment("break.dead");
            enterBlock(dead);
            return;
        }
        // 插入跳转到循环结束块的指令
        insert(createBranchInst(ctx.breakLabel));
        // 创建继续块以保持结构完整
        Block* cont = createBlock();
        cont->setComment("break.cont");
        // 进入继续块以保持结构完整
        enterBlock(cont);
    }

    void ASTCodeGen::visit(FE::AST::ContinueStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 continue 的无条件跳转至循环步进/条件块
        (void)node;
        (void)m;
        LoopContext ctx = currentLoopContext();
        if (!curBlock) return;
        if (!curBlock->insts.empty() && curBlock->insts.back()->isTerminator())
        {
            Block* dead = createBlock();
            dead->setComment("continue.dead");  // 创建死代码块
            enterBlock(dead);
            return;
        }
        insert(createBranchInst(ctx.continueLabel));
        Block* cont = createBlock();
        cont->setComment("continue.cont");  // 创建继续块
        enterBlock(cont);
    }

    void ASTCodeGen::visit(FE::AST::ForStmt& node, Module* m)
    {
        // TODO(Lab 3-2): 生成 for 循环 IR（init/cond/body/step 基本块与循环标签）
        // todoneed先不管
        bool ownScope = node.init && node.init->isVarDeclStmt();  // 如果初始化是变量声明，创建新作用域
        if (ownScope)
        {
            name2reg.enterScope();
            ++scopeDepth;
        }

        // 变量初始化
        if (node.init) apply(*this, *node.init, m);

        // 创建基本块
        Block* condBlock = createBlock();
        condBlock->setComment("for.cond");
        Block* bodyBlock = createBlock();
        bodyBlock->setComment("for.body");
        Block* stepBlock = createBlock();
        stepBlock->setComment("for.step");
        Block* endBlock = createBlock();
        endBlock->setComment("for.end");

        if (!curBlock || curBlock->insts.empty() || !curBlock->insts.back()->isTerminator())
        {
            // 如果当前块没有终止指令，插入跳转到条件块
            insert(createBranchInst(condBlock->blockId));
        }

        enterBlock(condBlock);
        // 条件判断块
        if (node.cond)
        {
            // 生成条件表达式的 IR
            apply(*this, *node.cond, m);
            size_t   condReg  = getMaxReg();
            DataType condType = convert(node.cond->attr.val.value.type);
            auto     insts    = createTypeConvertInst(condType, DataType::I1, condReg);
            if (!insts.empty())
            {
                condReg = getMaxReg();
                for (auto* inst : insts) { insert(inst); }
            }
            // 插入条件分支指令
            insert(createBranchInst(condReg, bodyBlock->blockId, endBlock->blockId));
        }
        else
        {
            // 没有条件，直接跳转到循环体块
            insert(createBranchInst(bodyBlock->blockId));
        }

        // 设置循环上下文标签，continue 指向步进块，break 指向结束块
        pushLoopContext(stepBlock->blockId, endBlock->blockId);

        // 进入循环体块
        enterBlock(bodyBlock);
        // 生成循环体语句的 IR
        if (node.body) { apply(*this, *node.body, m); }
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
        {
            // 如果循环体块没有终止指令，插入跳转到步进块指令
            insert(createBranchInst(stepBlock->blockId));
        }

        enterBlock(stepBlock);
        if (node.step)
        {
            // 生成步进表达式的 IR
            apply(*this, *node.step, m);
        }
        if (curBlock && (curBlock->insts.empty() || !curBlock->insts.back()->isTerminator()))
        {
            // 如果步进块没有终止指令，插入跳转到条件块指令
            insert(createBranchInst(condBlock->blockId));
        }

        popLoopContext();

        enterBlock(endBlock);

        if (ownScope)
        {
            // 退出作用域
            name2reg.exitScope();
            --scopeDepth;
        }
    }
}  // namespace ME
