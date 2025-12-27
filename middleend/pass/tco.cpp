#include <middleend/pass/tco.h>
#include <middleend/module/ir_operand.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/visitor/utils/alloca_derived_visitor.h>
#include <middleend/visitor/utils/operand_replace_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <iterator>

namespace ME
{
    void TCOPass::runOnModule(Module& module)
    {
        for (auto* function : module.functions) eliminateTailRecursion(*function);
    }

    void TCOPass::runOnFunction(Function& function) { eliminateTailRecursion(function); }

    // void 型返回值的分支链判断
    bool TCOPass::isVoidReturnChain(Function& function, Block* start) const
    {
        if (!start) return false;

        // 判断从 start 开始的分支链是否最终返回 void
        std::unordered_set<size_t> visited; //  记录访问过的基本块，防止环路
        Block*                     cur = start;
        while (cur)
        {
            if (!visited.insert(cur->blockId).second) return false;
            if (cur->insts.size() != 1) return false;   // 必须只有一条指令
            Instruction* inst = cur->insts.back();      // 获取唯一指令
            if (inst->opcode == Operator::RET)
            {
                // 返回指令，检查是否返回 void
                auto* ret = static_cast<RetInst*>(inst);
                return ret->res == nullptr;
            }
            // 如果不是返回指令，则必须为无条件分支指令
            if (inst->opcode != Operator::BR_UNCOND) return false;
            auto* br = static_cast<BrUncondInst*>(inst);
            if (!br->target || br->target->getType() != OperandType::LABEL) return false;
            cur = function.getBlock(br->target->getLabelNum());
        }
        return false;
    }

    // 检查参数是否与对应的参数寄存器相同
    // 检查给定的参数（arg）是否与函数的指定参数（通过索引 idx 访问）相同
    bool TCOPass::isSameParamArg(const Function& function, size_t idx, Operand* arg) const
    {
        // 获取函数定义中的参数寄存器
        Operand* paramOp = function.funcDef->argRegs[idx].second;
        if (!paramOp || !arg) return false;
        if (paramOp->getType() != arg->getType()) return false;
        if (paramOp->getType() == OperandType::REG)
        {
            return paramOp->getRegNum() == arg->getRegNum();
        }
        return paramOp == arg;
    }

    // 尾递归消除实现
    void TCOPass::eliminateTailRecursion(Function& function)
    {
        if (!function.funcDef) return;
        if (function.blocks.empty()) return;

        const std::string& funcName = function.funcDef->funcName;

        // 收集函数内 寄存器num -> 定义inst
        std::unordered_map<size_t, Instruction*> regDefs;
        for (auto& [id, block] : function.blocks)
        {
            for (auto* inst : block->insts)
            {
                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg != 0) regDefs.emplace(defReg, inst);
            }
        }

        // 创建 alloca 派生 检查器
        AllocaDerivedChecker allocaChecker(regDefs);

        Block* entry = function.blocks.begin()->second;

        // 建立参数寄存器到栈槽的映射
        std::vector<Operand*>              paramSlots(function.funcDef->argRegs.size(), nullptr);   // 参数栈槽列表
        std::vector<size_t>                paramStorePos(function.funcDef->argRegs.size(), 0);  // 参数初始化 store 指令位置
        std::vector<size_t>                paramRegNums(function.funcDef->argRegs.size(), 0);   // 参数寄存器号列表
        std::unordered_map<size_t, size_t> regToIndex;  // 参数寄存器号 -> 参数索引
        for (size_t i = 0; i < function.funcDef->argRegs.size(); ++i)
        {
            // 遍历参数列表，记录寄存器号
            Operand* argOp = function.funcDef->argRegs[i].second;
            if (!argOp || argOp->getType() != OperandType::REG) return;
            // 记录对应的寄存器编号
            size_t regNum      = argOp->getRegNum();
            regToIndex[regNum] = i;
            paramRegNums[i]    = regNum;
        }

        // 扫描入口块，找到参数初始化的 store 指令位置
        size_t instIndex = 0;
        for (auto* inst : entry->insts)
        {
            // 遍历所有 store 指令，找到参数初始化的位置
            if (inst->opcode == Operator::STORE)
            {
                auto* store = static_cast<StoreInst*>(inst);
                if (store->val && store->val->getType() == OperandType::REG)
                {
                    auto   it  = regToIndex.find(store->val->getRegNum());
                    if (it != regToIndex.end())
                    {
                        // 如果store的位置是 参数寄存器，记录数据 和 指令位置
                        paramSlots[it->second]    = store->ptr;
                        paramStorePos[it->second] = instIndex;
                    }
                }
            }
            ++instIndex;
        }

        // 分析所有尾递归调用点，确定需要创建栈槽的参数
        bool              hasEligibleTailCall = false; // 是否存在符合条件的尾递归调用
        bool              needsSlotUpdate     = false;  // 是否需要更新栈槽
        std::vector<bool> paramNeedsSlot(function.funcDef->argRegs.size(), false); // 参数是否需要栈槽

        // 遍历所有块，分析所有尾递归调用点
        // 尾递归的形式包括：
        // 1. call + ret:  %r = call f(...); ret %r
        // 2. call + br -> ret void:  call f(...); br %L; L: ret void
        for (auto& [id, block] : function.blocks)
        {
            if (block->insts.size() < 2) continue;
            Instruction* termInst = block->insts.back();
            Instruction* callInst = *(std::next(block->insts.rbegin(), 1));
            if (callInst->opcode != Operator::CALL) continue;

            auto* call = static_cast<CallInst*>(callInst);
            if (call->funcName != funcName) continue;
            if (call->args.size() != paramSlots.size()) continue;
            if (allocaChecker.hasAllocaDerivedArg(call)) continue;

            bool isTailCall = false;
            if (termInst->opcode == Operator::RET)
            {
                // 直接跟ret，保证返回类型和值相同
                auto* ret = static_cast<RetInst*>(termInst);
                if (function.funcDef->retType == DataType::VOID)
                {
                    isTailCall = (ret->res == nullptr);
                }
                else
                {
                    if (ret->res && call->res &&
                        ret->res->getType() == OperandType::REG && call->res->getType() == OperandType::REG)
                    {
                        isTailCall = (ret->res->getRegNum() == call->res->getRegNum());
                    }
                }
            }
            else if (termInst->opcode == Operator::BR_UNCOND)
            {
                // 如果是无条件分支，则检查分支链是否最终返回 void
                // 不处理非 void 函数的分支链尾递归
                if (function.funcDef->retType == DataType::VOID)
                {
                    auto* br = static_cast<BrUncondInst*>(termInst);
                    if (br->target && br->target->getType() == OperandType::LABEL)
                    {
                        Block* retBlock = function.getBlock(br->target->getLabelNum());
                        isTailCall = isVoidReturnChain(function, retBlock);
                    }
                }
            }

            if (!isTailCall) continue;

            for (size_t i = 0; i < call->args.size(); ++i)
            {
                // 检查每个参数是否与对应的参数寄存器相同
                // 如果相同，说明参数未发生变化，无需为该参数创建栈槽
                if (isSameParamArg(function, i, call->args[i].second)) continue;
                if (paramSlots[i] == nullptr) paramNeedsSlot[i] = true;
                needsSlotUpdate = true;
            }
            hasEligibleTailCall = true;
        }

        if (!hasEligibleTailCall) return;

        // 为需要的参数创建栈槽
        std::vector<StoreInst*> newParamStores;
        for (size_t i = 0; i < paramNeedsSlot.size(); ++i)
        {
            // 如果不需要栈槽，跳过
            if (!paramNeedsSlot[i]) continue;

            // 创建栈槽，并在入口块前部插入 alloca 指令
            DataType argType = function.funcDef->argRegs[i].first;
            size_t   slotReg = function.getNewRegId();
            auto*    slotOp  = getRegOperand(slotReg);
            entry->insertFront(new AllocaInst(argType, slotOp));
            // 在入口块适当位置插入参数初始化的 store 指令
            newParamStores.push_back(new StoreInst(argType, function.funcDef->argRegs[i].second, slotOp));
            paramSlots[i] = slotOp; // 更新参数栈槽
        }
        if (!newParamStores.empty())
        {
            // 将新建的参数初始化 store 指令插入到入口块的合适位置，也就是原有参数初始化 store 之后
            auto insertPos = entry->insts.begin();
            while (insertPos != entry->insts.end() && (*insertPos)->opcode == Operator::ALLOCA) ++insertPos;
            // 没有原有 store，则插入到入口块的最后
            entry->insts.insert(insertPos, newParamStores.begin(), newParamStores.end());
        }

        // 计算最后一个参数初始化 store 的位置
        size_t lastParamStoreIdx = 0;   
        size_t initStoreIndex    = 0;   // 遍历入口块指令索引
        for (auto* inst : entry->insts)
        {
            if (inst->opcode == Operator::STORE)
            {
                auto* store = static_cast<StoreInst*>(inst);
                for (auto* slot : paramSlots)
                {
                    if (slot && store->ptr == slot)
                    {
                        lastParamStoreIdx = initStoreIndex;
                        break;
                    }
                }
            }
            ++initStoreIndex;
        }

        // 将入口块分为"参数初始化"与"循环头"
        Block* loopHeader = entry;
        if (!paramSlots.empty() && needsSlotUpdate)
        {
            Block* newHeader = function.createBlock();
            newHeader->setComment(funcName + ".tco");

            auto splitIt = entry->insts.begin();
            std::advance(splitIt, static_cast<long>(lastParamStoreIdx + 1));
            while (splitIt != entry->insts.end())
            {
                newHeader->insts.push_back(*splitIt);
                splitIt = entry->insts.erase(splitIt);
            }

            entry->insts.push_back(new BrUncondInst(getLabelOperand(newHeader->blockId)));
            loopHeader = newHeader;
        }

        Operand* loopLabel = getLabelOperand(loopHeader->blockId);
        bool     changed   = false;

        // 对新建栈槽的参数在循环头读取，并替换后续使用
        std::unordered_map<size_t, Operand*> replaceRegs;
        for (size_t i = 0; i < paramNeedsSlot.size(); ++i)
        {
            if (!paramNeedsSlot[i]) continue;
            DataType argType = function.funcDef->argRegs[i].first;
            size_t   loadReg = function.getNewRegId();
            auto*    loadOp  = getRegOperand(loadReg);
            loopHeader->insertFront(new LoadInst(argType, paramSlots[i], loadOp));
            replaceRegs[paramRegNums[i]] = loadOp;
        }

        if (!replaceRegs.empty())
        {
            OperandReplaceVisitor replaceVisitor(replaceRegs);
            for (auto& [id, block] : function.blocks)
            {
                if (block == entry && loopHeader != entry) continue;
                for (auto* inst : block->insts) { apply(replaceVisitor, *inst); }
            }
        }

        if (loopHeader != entry)
        {
            Operand* oldLabel = getLabelOperand(entry->blockId);
            Operand* newLabel = loopLabel;
            for (auto& [id, block] : function.blocks)
            {
                if (block == loopHeader) continue;
                for (auto* inst : block->insts)
                {
                    if (inst->opcode != Operator::PHI) break;
                    auto* phi = static_cast<PhiInst*>(inst);
                    auto  it  = phi->incomingVals.find(oldLabel);
                    if (it == phi->incomingVals.end()) continue;
                    auto val = it->second;
                    phi->incomingVals.erase(it);
                    phi->incomingVals[newLabel] = val;
                }
            }
        }

        // 将自调用尾递归改写为参数写回 + 跳转到循环头
        for (auto& [id, block] : function.blocks)
        {
            if (block->insts.size() < 2) continue;
            Instruction* termInst = block->insts.back();
            Instruction* callInst = *(std::next(block->insts.rbegin(), 1));
            if (callInst->opcode != Operator::CALL) continue;

            auto* call = static_cast<CallInst*>(callInst);
            if (call->funcName != funcName) continue;
            if (call->args.size() != paramSlots.size()) continue;
            if (allocaChecker.hasAllocaDerivedArg(call)) continue;

            Block* retBlock = nullptr;
            if (termInst->opcode == Operator::RET)
            {
                auto* ret = static_cast<RetInst*>(termInst);
                if (function.funcDef->retType == DataType::VOID)
                {
                    if (ret->res != nullptr) continue;
                }
                else
                {
                    if (!ret->res || !call->res) continue;
                    if (ret->res->getType() != OperandType::REG || call->res->getType() != OperandType::REG) continue;
                    if (ret->res->getRegNum() != call->res->getRegNum()) continue;
                }
            }
            else if (termInst->opcode == Operator::BR_UNCOND)
            {
                if (function.funcDef->retType != DataType::VOID) continue;
                auto* br = static_cast<BrUncondInst*>(termInst);
                if (!br->target || br->target->getType() != OperandType::LABEL) continue;
                auto* label = static_cast<LabelOperand*>(br->target);
                retBlock    = function.getBlock(label->lnum);
                if (!isVoidReturnChain(function, retBlock)) continue;
            }
            else { continue; }

            block->insts.pop_back();
            block->insts.pop_back();
            auto callArgs = call->args;
            delete termInst;
            delete call;

            for (size_t i = 0; i < paramSlots.size(); ++i)
            {
                auto& callArg = callArgs[i];
                if (isSameParamArg(function, i, callArg.second)) continue;
                if (!paramSlots[i]) continue;
                block->insts.push_back(new StoreInst(callArg.first, callArg.second, paramSlots[i]));
            }

            block->insts.push_back(new BrUncondInst(loopLabel));

            if (retBlock)
            {
                Operand* label = getLabelOperand(block->blockId);
                for (auto* inst : retBlock->insts)
                {
                    if (inst->opcode != Operator::PHI) break;
                    auto* phi = static_cast<PhiInst*>(inst);
                    phi->incomingVals.erase(label);
                }
            }
            changed = true;
        }

        if (changed) Analysis::AM.invalidate(function);
    }
}  // namespace ME
