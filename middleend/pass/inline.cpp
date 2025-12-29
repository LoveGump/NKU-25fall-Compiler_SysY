#include <middleend/pass/inline.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/clone_visitor.h>
#include <middleend/visitor/utils/rename_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace ME
{
    // 构建调用点处的操作数映射表 （寄存器号 -> 实参操作数）
    std::map<size_t, Operand*> InlinePass::buildOperandMap(Function& caller, Function& callee, CallInst* callInst)
    {
        // operandMap 的目标：
        // - 形参寄存器 id  -> 调用点实参 （用 caller 侧已有 operand）
        // - callee 内部其它寄存器 -> caller 侧新分配寄存器（避免编号冲突）
        std::map<size_t, Operand*> operandMap;  // 寄存器号 -> 实参操作数

        // 获取callee的形参列表
        auto& calleeArgs = callee.funcDef->argRegs;
        for (size_t i = 0; i < calleeArgs.size(); ++i)
        {
            auto* argReg = calleeArgs[i].second;
            if (argReg && argReg->getType() == OperandType::REG)
            {
                // callee形参的寄存器id 映射到 caller 调用点的实参操作数
                operandMap[argReg->getRegNum()] = callInst->args[i].second;
            }
        }

        // 收集被调函数内出现的所有寄存器（use/def），并为 未映射寄存器分配 caller 侧新编号
        std::set<size_t>      regs;         // calee 内所有寄存器号
        std::map<size_t, int> useCounts;    // 寄存器使用次数统计
        UseCollector          useCollector(useCounts);
        DefCollector          defCollector;
        for (auto& [id, block] : callee.blocks)
        {
            for (auto* inst : block->insts)
            {
                // 收集所有指令的 use/def 寄存器
                apply(useCollector, *inst);
                apply(defCollector, *inst);
                size_t def = defCollector.getResult();
                if (def != 0) { regs.insert(def); }
            }
        }
        for (auto& [reg, _] : useCounts) regs.insert(reg);

        for (auto reg : regs)
        {
            // 遍历所有寄存器，若未映射则分配新寄存器
            if (operandMap.find(reg) == operandMap.end())
            {
                // 为该寄存器分配 caller 侧新寄存器，并建立新的映射
                operandMap[reg] = getRegOperand(caller.getNewRegId());
            }
        }
        return operandMap;
    }

    // 更新后继块的 phi incoming
    void InlinePass::updatePhiSucc(Function& caller, Operand* target, size_t oldBlockId, size_t newBlockId)
    {
        if (!target || target->getType() != OperandType::LABEL) return;
        Block* block = caller.getBlock(target->getLabelNum());
        if (!block) return;
        Operand* oldOp = getLabelOperand(oldBlockId);
        Operand* newOp = getLabelOperand(newBlockId);
        for (auto* inst : block->insts)
        {
            // 如果是 phi 指令，更新 incoming 映射
            if (inst->opcode != Operator::PHI) break;
            auto* phi = static_cast<PhiInst*>(inst);
            auto  it  = phi->incomingVals.find(oldOp);
            if (it != phi->incomingVals.end())
            {
                // 删除就的，添加新的
                Operand* val = it->second;
                phi->incomingVals.erase(it);
                phi->incomingVals[newOp] = val;
            }
        }
    }

    // 将克隆后的 label 目标重映射到 caller 中新建的块编号
    // - br 指令：直接替换 target/trueTar/falseTar
    // - phi 指令：重映射 incoming 的 label
    void InlinePass::remapLabels(Instruction* inst)
    {
        if (auto* br = dynamic_cast<BrUncondInst*>(inst))
        {
            // 替换目标标签
            remapLabel(br->target);
        }
        else if (auto* br = dynamic_cast<BrCondInst*>(inst))
        {
            remapLabel(br->trueTar);
            remapLabel(br->falseTar);
        }
        else if (auto* phi = dynamic_cast<PhiInst*>(inst))
        {
            std::map<Operand*, Operand*> newIncoming;
            for (auto& [label, val] : phi->incomingVals)
            {
                // 遍历所有 incoming，重映射 label
                if (label && label->getType() == OperandType::LABEL)
                {
                    auto it = labelMap_.find(label->getLabelNum());
                    if (it != labelMap_.end())
                    {
                        // 获取映射后的操作数
                        newIncoming[getLabelOperand(it->second)] = val;
                        continue;
                    }
                }
                newIncoming[label] = val;
            }
            phi->incomingVals = std::move(newIncoming);
        }
    }

    // 重映射单个 label 操作数
    // target 为指针的引用，指向需要重映射的操作数
    void InlinePass::remapLabel(Operand*& target)
    {
        if (!target || target->getType() != OperandType::LABEL) return;
        // 找到映射关系
        auto it = labelMap_.find(target->getLabelNum());
        if (it != labelMap_.end())
        {
            // 重映射到新的标签操作数
            target = getLabelOperand(it->second);
        }
    }

    bool InlinePass::inlineCall(Function& caller, Block* callBlock, CallInst* callInst, Function& callee)
    {
        // 内联基本流程：
        // 1) 校验签名与返回约束
        // 2) 将 caller 中 call 所在块切分为"callBlock + afterCall"
        // 3) 为 callee 的每个块创建对应的新块，并克隆指令到 caller
        // 4) 重命名寄存器/重映射 label，处理返回值与 ret 汇合
        // 5) 处理 alloca 提升到 caller 入口，保证作用域与后续优化一致
        if (!callBlock || !callInst || callee.blocks.empty() || !callee.funcDef) return false;
        if (callee.funcDef->argRegs.size() != callInst->args.size()) return false;
        if (callee.funcDef->retType != callInst->retType) return false;

        // 检查返回值一致性：
        // 若调用点需要返回值（非 void），则被调函数的每条 ret 都必须带返回操作数
        if (callInst->res && callInst->retType != DataType::VOID)
        {
            for (auto& [_, block] : callee.blocks)
                for (auto* inst : block->insts)
                    if (inst->opcode == Operator::RET && !static_cast<RetInst*>(inst)->res) return false;
        }

        // 在块内查找 call 指令，将指令分为 call 之前和 call 之后两部分
        auto&                    insts = callBlock->insts;
        std::deque<Instruction*> beforeCall;
        std::deque<Instruction*> afterCallInsts;
        bool                     found = false;
        for (auto* inst : insts)
        {
            if (inst == callInst)
            {
                found = true;
                continue;  // 跳过 call 指令本身
            }
            if (!found)
            {
                // 调用函数之前的指令
                beforeCall.push_back(inst);
            }
            else
            {
                // 调用函数之后的指令
                afterCallInsts.push_back(inst);
            }
        }
        if (!found) return false;

        // 创建续接块 afterCall：承接 call 之后的原有指令序列
        Block* afterCall = caller.createBlock();
        afterCall->insts = afterCallInsts;
        // callBlock 只保留 call 之前的指令
        callBlock->insts = beforeCall;

        // callBlock 被切分后，afterCall 继承了原先 callBlock 的"后继关系"
        // 需要将后继块里 phi 的 incoming label 从 callBlock 改为 afterCall
        if (!afterCall->insts.empty())
        {
            //  更新其目标块的 phi incoming
            if (auto* br = dynamic_cast<BrUncondInst*>(afterCall->insts.back()))
            {
                // 无条件跳转
                updatePhiSucc(caller, br->target, callBlock->blockId, afterCall->blockId);
            }
            else if (auto* br = dynamic_cast<BrCondInst*>(afterCall->insts.back()))
            {
                updatePhiSucc(caller, br->trueTar, callBlock->blockId, afterCall->blockId);
                updatePhiSucc(caller, br->falseTar, callBlock->blockId, afterCall->blockId);
            }
        }

        // 为 callee 的每个基本块创建对应的新块（blockMap/labelMap_ 用于重映射跳转目标）
        std::map<size_t, Block*> blockMap;  // callee block id -> caller new block ptr
        labelMap_.clear();                  // callee block id -> caller new block id
        for (auto& [id, _] : callee.blocks)
        {
            Block* newBlock = caller.createBlock();
            blockMap[id]    = newBlock;
            labelMap_[id]   = newBlock->blockId;
        }

        // 将 callBlock 末尾改为跳转到 内联后的 callee 入口块
        // 其实这里为了避免有的优化将入口块改了
        size_t entryId = callee.blocks.count(0) ? 0 : callee.blocks.begin()->first;
        callBlock->insertBack(new BrUncondInst(getLabelOperand(blockMap[entryId]->blockId)));

        // callee id -> caller operand
        operandMap_ = buildOperandMap(caller, callee, callInst);  

        // 有了映射表之后，开始进行指令的克隆与重命名
        OperandRename renamer;
        InstCloner    cloner;

        // 返回值汇合点：若 call 需要返回值，则在 afterCall 插入一个 phi
        // 每个 ret 分支向该 phi 添加 incoming，然后统一跳转到 afterCall
        PhiInst* retPhi = nullptr;
        if (callInst->res && callInst->retType != DataType::VOID)
        {
            retPhi = new PhiInst(callInst->retType, callInst->res);
            afterCall->insts.push_front(retPhi);
        }

        // 获取 afterCall 的标签操作数，用于 ret 指令跳转
        Operand* afterLabel = getLabelOperand(afterCall->blockId);

        // alloca 提升：将内联体中的 alloca 统一挪到 caller 的入口块
        // 避免 alloca 散落在中间块影响后续优化
        Block* entryBlockForAllocas = nullptr;
        if (!caller.blocks.empty())
        {
            auto it              = caller.blocks.find(0);
            entryBlockForAllocas = it != caller.blocks.end() ? it->second : caller.blocks.begin()->second;
        }
        // 收集待提升的 alloca 指令，先收集后面统一插入避免时间复杂度过高
        std::vector<Instruction*> hoistedAllocas;

        // 克隆每个 callee 块内的指令到对应的新块：
        // - ret：转成"向 retPhi 添 incoming + br 到 afterCall"
        // - 其它指令：克隆后做寄存器重命名与 label 重映射
        for (auto& [id, block] : callee.blocks)
        {
            Block* newBlock = blockMap[id];  // 获取对应的新块
            for (auto* inst : block->insts)
            {
                if (inst->opcode == Operator::RET)
                {
                    // 将 ret 指令 返回的值 添加到 retPhi
                    auto* ret = static_cast<RetInst*>(inst);
                    if (retPhi && ret->res)
                    {
                        // 有返回值
                        Operand* mappedRes = ret->res; // 在callee侧 的返回值操作数
                        if (mappedRes && mappedRes->getType() == OperandType::REG)
                        {
                            auto it = operandMap_.find(mappedRes->getRegNum());
                            if (it != operandMap_.end())
                            {
                                // 根据 operandMap_ 重命名到 caller 侧的返回值操作数
                                mappedRes = it->second;
                            }
                        }
                        // 插入新的 incoming 映射
                        retPhi->addIncoming(mappedRes, getLabelOperand(newBlock->blockId));
                    }
                    // 插入无条件跳转到 afterCall
                    newBlock->insertBack(new BrUncondInst(afterLabel));
                    continue;
                }

                // 克隆其余指令，并进行寄存器重命名与 label 重映射
                Instruction* cloned = apply(cloner, *inst);
                if (!cloned) continue;
                // 根据 operandMap_ 重命名寄存器，根据 labelMap_ 重映射标签
                apply(renamer, *cloned, operandMap_);
                remapLabels(cloned);

                // 如果是 alloca 指令，暂时先不插入块，后续统一提升到入口块
                if (cloned->opcode == Operator::ALLOCA && entryBlockForAllocas) { hoistedAllocas.push_back(cloned); }
                else { newBlock->insertBack(cloned); }
            }
        }

        // 将收集到的 alloca 插入到入口块最后一个 alloca 之后
        if (entryBlockForAllocas && !hoistedAllocas.empty())
        {
            auto lastAlloca = entryBlockForAllocas->insts.begin();
            for (auto it = entryBlockForAllocas->insts.begin(); it != entryBlockForAllocas->insts.end(); ++it)
            {
                if ((*it)->opcode == Operator::ALLOCA) lastAlloca = std::next(it);
            }
            entryBlockForAllocas->insts.insert(lastAlloca, hoistedAllocas.begin(), hoistedAllocas.end());
        }

        return true;
    }

    void InlinePass::runOnModule(Module& module)
    {
        // 模块级内联：
        // - 每轮分析调用图与代价信息
        // - 按策略给出的处理顺序，尝试内联满足条件的调用点
        // - 内联会改变调用图与函数规模，循环迭代直到收敛
        module_ = &module;
        InlineStrategy strategy;
        bool           changed = true;

        while (changed)
        {
            // 每轮根据当前模块重新统计调用图与代价信息
            // 内联会改变函数体结构与调用次数分布，因此需要循环分析 + 尝试内联，直到不再发生变化
            strategy.analyze(module);
            changed = false;

            for (auto* func : strategy.getProcessingOrder())
            {
                if (!func) continue;

                // 预先收集待内联调用点（只记录指针）
                // 内联过程中会切分块并移动指令，提前保存 (block, index) 容易失效
                // 因此这里只保存 call 指令地址与 callee，之后再在函数内重新定位
                std::vector<std::pair<CallInst*, Function*>> calls;

                for (auto& [_, block] : func->blocks)
                {
                    if (!block) continue;
                    for (auto* inst : block->insts)
                    {
                        auto* call = dynamic_cast<CallInst*>(inst);
                        if (!call) continue;
                        auto* callee = strategy.findFunction(call->funcName);
                        if (callee && strategy.shouldInline(*func, *callee, *call)) calls.emplace_back(call, callee);
                    }
                }

                for (auto& [call, callee] : calls)
                {
                    // 定位 call 指令所在的块（之前的内联可能导致块切分/指令移动）
                    Block* foundBlock = nullptr;
                    for (auto& [_, blk] : func->blocks)
                    {
                        if (!blk) continue;
                        if (std::find(blk->insts.begin(), blk->insts.end(), static_cast<Instruction*>(call)) !=
                            blk->insts.end())
                        {
                            foundBlock = blk;
                            break;
                        }
                    }
                    if (!foundBlock) continue;

                    if (inlineCall(*func, foundBlock, call, *callee))
                    {
                        changed = true;
                        Analysis::AM.invalidate(*func);
                    }
                }
            }
        }
        module_ = nullptr;
    }

    void InlinePass::runOnFunction(Function& function)
    {
        (void)function;
        // 内联作为模块级别的优化，不在函数级别执行任何操作
    }
}  // namespace ME

/*
InlinePass 流程总结（对应本文件实现）：
1) 决策阶段由 InlineStrategy 完成：给出处理顺序与 shouldInline 判定。
2) 对每个调用点执行 inlineCall：
   - 将 call 所在块切分为“call 之前(callBlock)”与“call 之后(afterCall)”；
   - 为 callee 的每个块创建一个 caller 侧新块，并建立 labelMap_ 用于跳转重映射；
   - 建立 operandMap_：形参->实参，callee 内部寄存器->caller 新寄存器；
   - 克隆 callee 指令到新块：重命名寄存器、重映射 label；
   - ret 改写：若有返回值，在 afterCall 插 Phi 汇合各 ret 的返回值，然后各 ret 分支跳转 afterCall；
   - alloca 提升：把内联体的 alloca 统一搬到 caller 入口块，便于后续 mem2reg 等优化。
3) 内联会改变 CFG/分析结果：成功内联后对函数的分析缓存进行失效处理。
*/
