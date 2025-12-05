#include <middleend/visitor/codegen/ast_codegen.h>
#include <sstream>
#include <string>

namespace ME
{
    /**
     * 为左值表达式创建地址操作数：指向变量或数组元素的地址 前导0
     * @param node 左值表达式节点
     * @param m    当前模块
     * @return 地址操作数
     */
    Operand* ASTCodeGen::ensureLValueAddress(FE::AST::LeftValExpr& node, Module* m)
    {
        auto* entry = node.entry;  // 获取符号表项

        const FE::AST::VarAttr* attr = getVarAttr(entry);  // 获取变量属性

        Operand* basePtr = nullptr;
        // 如果是全局变量
        if (attr->scopeLevel == -1)
        {
            // 全局变量的地址直接通过全局操作数获取
            basePtr = getGlobalOperand(entry->getName());
        }
        else
        {
            // 如果是局部变量，通过符号表获取寄存器号
            size_t reg = name2reg.getReg(entry);
            ASSERT(reg != static_cast<size_t>(-1) && "Local symbol without register binding");
            // 获取寄存器操作数作为地址
            basePtr = getRegOperand(reg);
        }
        // 非数组变量直接返回地址
        if (attr->arrayDims.empty()) { return basePtr; }

        // 对于数组变量，生成 GEP 指令计算具体元素地址
        std::vector<int> filteredDims;
        filteredDims.reserve(attr->arrayDims.size());
        for (int dim : attr->arrayDims)
        {
            // 如果数组下标的维度小于等于0
            if (dim <= 0) continue;
            filteredDims.push_back(dim);
        }

        // 生成下标操作数列表
        std::vector<Operand*> idxOps;
        // 如果数组有维度且首维度大于0，则需要一个前导的0索引
        bool needLeadingZero = !attr->arrayDims.empty() && attr->arrayDims.front() > 0;
        if (needLeadingZero)
        {
            // 添加前导0索引
            idxOps.push_back(getImmeI32Operand(0));
        }

        if (node.indices)
        {
            // 处理数组下标表达式，对变量的处理
            for (auto* idxNode : *(node.indices))
            {
                // 访问下标表达式，生成对应的 IR
                if (!idxNode) continue;
                apply(*this, *idxNode, m);
                size_t   idxReg  = getMaxReg();
                DataType idxType = convert(idxNode->attr.val.value.type);
                auto     insts   = createTypeConvertInst(idxType, DataType::I32, idxReg);
                if (!insts.empty())
                {
                    idxReg = getMaxReg();  // 使用新的寄存器来存放变量
                    for (auto* inst : insts) { 
                        insert(inst); 
                    }
                }
                // 添加下标操作数
                idxOps.push_back(getRegOperand(idxReg));
            }
        }

        // 如果没有下标，直接返回基址
        Operand* ptr = basePtr;
        if (!idxOps.empty())
        {
            // 生成 GEP 指令计算地址
            size_t resReg = getNewRegId();
            insert(createGEP_I32Inst(convert(attr->type), basePtr, filteredDims, idxOps, resReg));
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

        size_t   resReg   = getNewRegId();  // 结果寄存器
        DataType loadType = convert(type);  // 加载类型
        // 发出 load 指令，将地址内容加载到新申请的寄存器
        insert(createLoadInst(loadType, ptr, resReg));
    }

    void ASTCodeGen::visit(FE::AST::LiteralExpr& node, Module* m)
    {
        // 常量表达式的 IR 生成
        (void)m;

        // 生成常量表达式的寄存器和指令
        size_t reg = getNewRegId();  // 申请新的寄存器
        switch (node.literal.type->getBaseType())
        {  // 根据不同的基础类型生成相应的指令
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
            default: ERROR("Unsupported literal type at line %d, col %d", node.line_num, node.col_num);
        }
    }

    void ASTCodeGen::visit(FE::AST::UnaryExpr& node, Module* m)
    {
        // TODO(Lab 3-2): 生成一元运算的 IR（访问操作数、必要的类型转换、发出运算指令）
        if (!node.expr) return;
        handleUnaryCalc(*node.expr, node.op, curBlock, m);
    }

    // 生成赋值语句的 IR（计算右值、类型转换、store 到左值地址）
    void ASTCodeGen::handleAssign(FE::AST::LeftValExpr& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        // TODO(Lab 3-2): 生成赋值语句的 IR（计算右值、类型转换、store 到左值地址）
        Operand* ptr = ensureLValueAddress(lhs, m);  // 获取左值地址
        // 计算右值表达式，生成对应的 IR
        apply(*this, rhs, m);
        // 获取右值寄存器和类型
        size_t   rhsReg  = getMaxReg();
        DataType rhsType = convert(rhs.attr.val.value.type);

        // 获取左值类型并进行类型转换
        DataType lhsType = convert(lhs.attr.val.value.type);
        auto     insts   = createTypeConvertInst(rhsType, lhsType, rhsReg);
        if (!insts.empty())
        {
            rhsReg = getMaxReg();  // 使用新的寄存器来存放结果
            for (auto* inst : insts) { insert(inst); }
        }
        // 发出 store 指令，将右值存储到左值地址
        insert(createStoreInst(lhsType, rhsReg, ptr));
    }

    // 生成短路与的基本块与条件分支
    void ASTCodeGen::handleLogicalAnd(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        // TODO(Lab 3-2): 生成短路与的基本块与条件分支
        (void)node;
        // 计算左操作数表达式
        apply(*this, lhs, m);
        size_t   lhsReg    = getMaxReg();                       // 获取左操作数寄存器
        DataType lhsType   = convert(lhs.attr.val.value.type);  // 获取左操作数类型
        auto     leftinsts = createTypeConvertInst(lhsType, DataType::I1, lhsReg);
        if (!leftinsts.empty())
        {
            lhsReg = getMaxReg();  // 使用新的寄存器来存放变量
            for (auto* inst : leftinsts) { insert(inst); }
        }
        // 保存当前块作为左操作数块
        Block* lhsBlock = curBlock;
        // 创建右操作数块和结束块
        Block* rhsEntryBlock = createBlock();
        rhsEntryBlock->setComment("and.rhs");
        Block* endBlock = createBlock();
        endBlock->setComment("and.end");

        // 根据左操作数结果决定跳转到右操作数块或结束块
        insert(createBranchInst(lhsReg, rhsEntryBlock->blockId, endBlock->blockId));

        // 进入右操作数块，计算右操作数表达式
        enterBlock(rhsEntryBlock);
        apply(*this, rhs, m);
        size_t   rhsReg     = getMaxReg();
        DataType rhsType    = convert(rhs.attr.val.value.type);
        auto     rightinsts = createTypeConvertInst(rhsType, DataType::I1, rhsReg);
        if (!rightinsts.empty())
        {
            rhsReg = getMaxReg();  // 使用新的寄存器来存放变量
            for (auto* inst : rightinsts) { insert(inst); }
        }
        Block* rhsExitBlock = curBlock;
        if (rhsExitBlock && (rhsExitBlock->insts.empty() || !rhsExitBlock->insts.back()->isTerminator()))
        {
            insert(createBranchInst(endBlock->blockId));
        }
        // 进入结束块，生成 Phi 指令合并结果
        enterBlock(endBlock);
        size_t resultReg = getNewRegId();  // 结果寄存器
        // 生成 Phi 指令，根据不同路径选择结果
        PhiInst* phi = new PhiInst(DataType::I1, getRegOperand(resultReg));
        phi->addIncoming(getRegOperand(lhsReg), getLabelOperand(lhsBlock->blockId));
        phi->addIncoming(getRegOperand(rhsReg), getLabelOperand(rhsExitBlock->blockId));
        insert(phi);
    }

    // 生成短路或的基本块与条件分支
    void ASTCodeGen::handleLogicalOr(
        FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m)
    {
        // TODO(Lab 3-2): 生成短路或的基本块与条件分支
        (void)node;
        // 计算左操作数表达式
        apply(*this, lhs, m);
        size_t   lhsReg    = getMaxReg();
        DataType lhsType   = convert(lhs.attr.val.value.type);
        auto     leftinsts = createTypeConvertInst(lhsType, DataType::I1, lhsReg);
        if (!leftinsts.empty())
        {
            lhsReg = getMaxReg();  // 使用新的寄存器来存放变量
            for (auto* inst : leftinsts) { insert(inst); }
        }
        // 保存当前块作为左操作数块
        Block* lhsBlock = curBlock;

        // 创建右操作数块和结束块
        Block* rhsEntryBlock = createBlock();
        rhsEntryBlock->setComment("or.rhs");
        Block* endBlock = createBlock();
        endBlock->setComment("or.end");

        // 根据左操作数结果决定跳转到结束块或右操作数块
        insert(createBranchInst(lhsReg, endBlock->blockId, rhsEntryBlock->blockId));

        // 进入右操作数块，计算右操作数表达式
        enterBlock(rhsEntryBlock);
        apply(*this, rhs, m);
        size_t   rhsReg     = getMaxReg();
        DataType rhsType    = convert(rhs.attr.val.value.type);
        auto     rightinsts = createTypeConvertInst(rhsType, DataType::I1, rhsReg);
        if (!rightinsts.empty())
        {
            rhsReg = getMaxReg();
            for (auto* inst : rightinsts) { insert(inst); }
        }

        Block* rhsExitBlock = curBlock;
        if (rhsExitBlock && (rhsExitBlock->insts.empty() || !rhsExitBlock->insts.back()->isTerminator()))
        {
            // 右操作数块没有终止指令，跳转到结束块
            insert(createBranchInst(endBlock->blockId));
        }

        enterBlock(endBlock);
        // 进入结束块，生成 Phi 指令合并结果
        size_t   resultReg = getNewRegId();
        PhiInst* phi       = new PhiInst(DataType::I1, getRegOperand(resultReg));
        phi->addIncoming(getRegOperand(lhsReg), getLabelOperand(lhsBlock->blockId));
        phi->addIncoming(getRegOperand(rhsReg), getLabelOperand(rhsExitBlock->blockId));
        insert(phi);
    }

    void ASTCodeGen::visit(FE::AST::BinaryExpr& node, Module* m)
    {
        // TODO(Lab 3-2): 生成二元表达式 IR（含赋值、逻辑与/或、算术/比较）
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
        // TODO(Lab 3-2): 生成函数调用 IR（准备参数、可选返回寄存器、发出call）
        // 获取函数名称
        std::string funcName = node.func->getName();

        // 获取函数声明以便参数类型检查
        FE::AST::FuncDeclStmt* decl = funcDecls.at(node.func);

        // 函数调用语句的参数数量
        size_t            argCount = node.args ? node.args->size() : 0;
        CallInst::argList args;  // 函数参数列表<类型, 操作数>

        for (size_t i = 0; i < argCount; ++i)
        {
            // 遍历每个参数表达式
            auto* argNode = (*node.args)[i];
            // 获取参数表达式节点
            if (!argNode) continue;

            // 生成参数表达式的 IR
            apply(*this, *argNode, m);

            // 对于声明语句，解析函数类型和维度信息
            // 解析期望的参数类型和维度信息
            FE::AST::Type*   expectedType = nullptr;
            std::vector<int> expectedDims;
            if (decl && decl->params)
            {                                          // 根据函数声明获取期望的参数类型
                auto* paramDecl = (*decl->params)[i];  // 获取对应的函数声明节点
                if (paramDecl)
                {
                    expectedType = paramDecl->type;  // 期望的类型
                    if (paramDecl->dims)
                    {
                        // 收集期望的数组维度
                        for (auto* expr : *(paramDecl->dims))
                        {
                            if (!expr) continue;
                            expectedDims.push_back(expr->attr.val.getInt());
                        }
                    }
                }
            }

            // 获取实际参数类型
            FE::AST::Type* argType = argNode->attr.val.value.type;  // 参数类型
            DataType       argDT   = convert(argType);              // 转换为 IR 数据类型

            if (argType && argType->getTypeGroup() == FE::AST::TypeGroup::POINTER)
            {  // 指针类型参数处理
                // 获取指针指向类型的数组维度
                auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(argNode);
                args.emplace_back(DataType::PTR, ensureLValueAddress(*lval, m));
            }
            else
            {
                // 不是数组参数，进行类型转换
                size_t   argReg   = getMaxReg();  // 获取参数
                DataType expectDT = convert(expectedType);
                auto     insts    = createTypeConvertInst(argDT, expectDT, argReg);  // 类型转换指令
                if (!insts.empty())
                {
                    argReg = getMaxReg();  // 使用新的寄存器来存放变量
                    for (auto* inst : insts) { insert(inst); }
                }
                args.emplace_back(expectDT, getRegOperand(argReg));
            }
        }

        // 处理返回值
        DataType retType = convert(decl->retType);
        if (retType == DataType::VOID)
        {
            CallInst* call = createCallInst(retType, funcName, args);
            insert(call);
        }
        else
        {
            // 处理有返回值的函数调用
            size_t    resReg = getNewRegId();
            CallInst* call   = createCallInst(retType, funcName, args, resReg);
            insert(call);
        }
    }

    void ASTCodeGen::visit(FE::AST::CommaExpr& node, Module* m)
    {
        // TODO(Lab 3-2): 依序生成逗号表达式每个子表达式的 IR
        if (!node.exprs) return;
        for (auto* expr : *(node.exprs))
        {
            if (!expr) continue;
            apply(*this, *expr, m);
        }
    }
}  // namespace ME
