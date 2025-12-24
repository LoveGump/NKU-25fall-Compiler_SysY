#include <middleend/pass/tco.h>
#include <middleend/module/ir_operand.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/visitor/utils/alloca_derived_visitor.h>
#include <middleend/visitor/utils/operand_replace_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <iterator>

namespace ME
{
    void TCOPass::runOnModule(Module& module)
    {
        for (auto* function : module.functions) eliminateTailRecursion(*function);
    }

    void TCOPass::runOnFunction(Function& function) { eliminateTailRecursion(function); }

    bool TCOPass::isVoidReturnChain(Function& function, Block* start) const
    {
        if (!start) return false;

        std::unordered_set<size_t> visited;
        Block* cur = start;
        while (cur)
        {
            if (!visited.insert(cur->blockId).second) return false;
            if (cur->insts.size() != 1) return false;
            Instruction* inst = cur->insts.back();
            if (inst->opcode == Operator::RET)
            {
                auto* ret = static_cast<RetInst*>(inst);
                return ret->res == nullptr;
            }
            if (inst->opcode != Operator::BR_UNCOND) return false;
            auto* br = static_cast<BrUncondInst*>(inst);
            if (!br->target || br->target->getType() != OperandType::LABEL) return false;
            auto* label = static_cast<LabelOperand*>(br->target);
            cur = function.getBlock(label->lnum);
        }
        return false;
    }

    bool TCOPass::isSameParamArg(const Function& function, size_t idx, Operand* arg) const
    {
        // 判断调用参数是否等同于原始形参，避免不必要的写回
        Operand* paramOp = function.funcDef->argRegs[idx].second;
        if (!paramOp || !arg) return false;
        if (paramOp->getType() != arg->getType()) return false;
        if (paramOp->getType() == OperandType::REG)
        {
            return static_cast<RegOperand*>(paramOp)->regNum == static_cast<RegOperand*>(arg)->regNum;
        }
        return paramOp == arg;
    }

    bool TCOPass::isAllocaDerivedReg(size_t regNum, const std::unordered_map<size_t, Instruction*>& regDefs,
        std::unordered_map<size_t, bool>& memo, std::unordered_set<size_t>& visiting) const
    {
        // 递归判断寄存器是否源自 alloca 指针，防止错误的尾递归展开
        auto memoIt = memo.find(regNum);
        if (memoIt != memo.end()) return memoIt->second;
        if (!visiting.insert(regNum).second) return false;

        bool derived = false;
        auto defIt   = regDefs.find(regNum);
        if (defIt != regDefs.end())
        {
            // 通过访问者递归判断该寄存器是否由 alloca 派生
            AllocaDerivedVisitor::RegChecker checker =
                std::bind(&TCOPass::isAllocaDerivedReg, this, std::placeholders::_1, std::cref(regDefs),
                    std::ref(memo), std::ref(visiting));
            AllocaDerivedVisitor visitor(checker);
            derived = apply(visitor, *defIt->second);
        }

        visiting.erase(regNum);
        memo[regNum] = derived;
        return derived;
    }

    bool TCOPass::hasAllocaDerivedArg(CallInst* call, const std::unordered_map<size_t, Instruction*>& regDefs,
        std::unordered_map<size_t, bool>& memo, std::unordered_set<size_t>& visiting) const
    {
        // 调用参数中出现栈上地址派生指针时禁止做 TCO
        if (!call) return false;
        for (auto& arg : call->args)
        {
            Operand* op = arg.second;
            if (!op || op->getType() != OperandType::REG) continue;
            size_t regNum = static_cast<RegOperand*>(op)->regNum;
            if (isAllocaDerivedReg(regNum, regDefs, memo, visiting)) return true;
        }
        return false;
    }

    void TCOPass::eliminateTailRecursion(Function& function)
    {
        if (!function.funcDef) return;
        if (function.blocks.empty()) return;

        const std::string& funcName = function.funcDef->funcName;

        std::unordered_map<size_t, Instruction*> regDefs;
        regDefs.reserve(function.blocks.size() * 8);
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

        std::unordered_map<size_t, bool> allocaDerivedMemo;
        std::unordered_set<size_t>       allocaDerivedVisiting;

        // 先扫描是否存在自调用尾递归
        bool hasTailCall = false;
        for (auto& [id, block] : function.blocks)
        {
            if (block->insts.size() < 2) continue;
            Instruction* termInst = block->insts.back();
            Instruction* callInst = *(std::next(block->insts.rbegin(), 1));
            if (callInst->opcode != Operator::CALL) continue;

            auto* call = static_cast<CallInst*>(callInst);
            if (call->funcName != funcName) continue;
            if (hasAllocaDerivedArg(call, regDefs, allocaDerivedMemo, allocaDerivedVisiting)) continue;

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
                    size_t retReg  = static_cast<RegOperand*>(ret->res)->regNum;
                    size_t callReg = static_cast<RegOperand*>(call->res)->regNum;
                    if (retReg != callReg) continue;
                }
            }
            else if (termInst->opcode == Operator::BR_UNCOND)
            {
                // 允许 “call + br -> ret void” 形式的尾递归
                if (function.funcDef->retType != DataType::VOID) continue;
                auto* br = static_cast<BrUncondInst*>(termInst);
                if (!br->target || br->target->getType() != OperandType::LABEL) continue;
                auto* label = static_cast<LabelOperand*>(br->target);
                Block* retBlock = function.getBlock(label->lnum);
                if (!isVoidReturnChain(function, retBlock)) continue;
            }
            else
            {
                continue;
            }

            hasTailCall = true;
            break;
        }

        if (!hasTailCall) return;

        Block* entry = function.blocks.begin()->second;

        // 建立参数寄存器到栈槽的映射（仅处理能找到参数存储的函数）
        // 只有“发生变化的参数”才需要栈槽支持
        std::vector<Operand*> paramSlots(function.funcDef->argRegs.size(), nullptr);
        std::vector<size_t>   paramStorePos(function.funcDef->argRegs.size(), 0);
        std::vector<size_t>   paramRegNums(function.funcDef->argRegs.size(), 0);
        std::unordered_map<size_t, size_t> regToIndex;
        for (size_t i = 0; i < function.funcDef->argRegs.size(); ++i)
        {
            Operand* argOp = function.funcDef->argRegs[i].second;
            if (!argOp || argOp->getType() != OperandType::REG) return;
            size_t regNum = static_cast<RegOperand*>(argOp)->regNum;
            regToIndex[regNum] = i;
            paramRegNums[i]    = regNum;
        }

        size_t instIndex = 0;
        for (auto* inst : entry->insts)
        {
            if (inst->opcode == Operator::STORE)
            {
                auto* store = static_cast<StoreInst*>(inst);
                if (store->val && store->val->getType() == OperandType::REG)
                {
                    size_t reg = static_cast<RegOperand*>(store->val)->regNum;
                    auto   it  = regToIndex.find(reg);
                    if (it != regToIndex.end())
                    {
                        paramSlots[it->second] = store->ptr;
                        paramStorePos[it->second] = instIndex;
                    }
                }
            }
            ++instIndex;
        }

        bool   hasEligibleTailCall = false;
        bool   needsSlotUpdate     = false;
        std::vector<bool> paramNeedsSlot(function.funcDef->argRegs.size(), false);
        size_t lastParamStoreIdx   = 0;
        for (auto& [id, block] : function.blocks)
        {
            if (block->insts.size() < 2) continue;
            Instruction* termInst = block->insts.back();
            Instruction* callInst = *(std::next(block->insts.rbegin(), 1));
            if (callInst->opcode != Operator::CALL) continue;

            auto* call = static_cast<CallInst*>(callInst);
            if (call->funcName != funcName) continue;
            if (call->args.size() != paramSlots.size()) continue;
            if (hasAllocaDerivedArg(call, regDefs, allocaDerivedMemo, allocaDerivedVisiting)) continue;

            bool callConvertible = true;
            bool callUpdatesSlot = false;
            for (size_t i = 0; i < call->args.size(); ++i)
            {
                if (isSameParamArg(function, i, call->args[i].second)) continue;
                if (paramSlots[i] == nullptr) { paramNeedsSlot[i] = true; }
                callUpdatesSlot = true;
                if (paramStorePos[i] > lastParamStoreIdx) lastParamStoreIdx = paramStorePos[i];
            }
            if (!callConvertible) continue;

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
                    size_t retReg  = static_cast<RegOperand*>(ret->res)->regNum;
                    size_t callReg = static_cast<RegOperand*>(call->res)->regNum;
                    if (retReg != callReg) continue;
                }
            }
            else if (termInst->opcode == Operator::BR_UNCOND)
            {
                if (function.funcDef->retType != DataType::VOID) continue;
                auto* br = static_cast<BrUncondInst*>(termInst);
                if (!br->target || br->target->getType() != OperandType::LABEL) continue;
                auto* label = static_cast<LabelOperand*>(br->target);
                Block* retBlock = function.getBlock(label->lnum);
                if (!isVoidReturnChain(function, retBlock)) continue;
            }
            else
            {
                continue;
            }

            hasEligibleTailCall = true;
            if (callUpdatesSlot) needsSlotUpdate = true;
            break;
        }

        if (!hasEligibleTailCall) return;

        // 为需要的参数创建栈槽，便于后续通过 store 更新
        std::vector<StoreInst*> newParamStores;
        for (size_t i = 0; i < paramNeedsSlot.size(); ++i)
        {
            if (!paramNeedsSlot[i]) continue;
            DataType argType = function.funcDef->argRegs[i].first;
            size_t   slotReg = function.getNewRegId();
            auto*    slotOp  = getRegOperand(slotReg);
            entry->insertFront(new AllocaInst(argType, slotOp));
            newParamStores.push_back(new StoreInst(argType, function.funcDef->argRegs[i].second, slotOp));
            paramSlots[i] = slotOp;
            needsSlotUpdate = true;
        }
        if (!newParamStores.empty())
        {
            auto insertPos = entry->insts.begin();
            while (insertPos != entry->insts.end() && (*insertPos)->opcode == Operator::ALLOCA) ++insertPos;
            entry->insts.insert(insertPos, newParamStores.begin(), newParamStores.end());
        }

        // 重新计算最后一个参数初始化 store 的位置
        lastParamStoreIdx = 0;
        size_t initStoreIndex = 0;
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

        // 将入口块分为“参数初始化”与“循环头”
        // 递归跳转应避开参数初始化，避免覆盖新参数
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
            // 入口块不再直接连接原后继，需把 Phi 的入口标签替换为循环头
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
        // 只处理“call + ret”紧邻的尾递归形式
        for (auto& [id, block] : function.blocks)
        {
            if (block->insts.size() < 2) continue;
            Instruction* termInst = block->insts.back();
            Instruction* callInst = *(std::next(block->insts.rbegin(), 1));
            if (callInst->opcode != Operator::CALL) continue;

            auto* call = static_cast<CallInst*>(callInst);
            if (call->funcName != funcName) continue;
            if (call->args.size() != paramSlots.size()) continue;
            if (hasAllocaDerivedArg(call, regDefs, allocaDerivedMemo, allocaDerivedVisiting)) continue;

            Block* retBlock = nullptr;
            bool   deleteRet = false;
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
                    size_t retReg  = static_cast<RegOperand*>(ret->res)->regNum;
                    size_t callReg = static_cast<RegOperand*>(call->res)->regNum;
                    if (retReg != callReg) continue;
                }
                deleteRet = true;
            }
            else if (termInst->opcode == Operator::BR_UNCOND)
            {
                if (function.funcDef->retType != DataType::VOID) continue;
                auto* br = static_cast<BrUncondInst*>(termInst);
                if (!br->target || br->target->getType() != OperandType::LABEL) continue;
                auto* label = static_cast<LabelOperand*>(br->target);
                retBlock = function.getBlock(label->lnum);
                if (!isVoidReturnChain(function, retBlock)) continue;
            }
            else
            {
                continue;
            }

            // 移除 call + terminator，并插入参数写回
            block->insts.pop_back();
            block->insts.pop_back();
            auto callArgs = call->args;
            if (deleteRet)
            {
                delete static_cast<RetInst*>(termInst);
            }
            delete call;

            for (size_t i = 0; i < paramSlots.size(); ++i)
            {
                auto& callArg = callArgs[i];
                if (isSameParamArg(function, i, callArg.second)) continue;
                if (!paramSlots[i]) continue;
                block->insts.push_back(new StoreInst(callArg.first, callArg.second, paramSlots[i]));
            }

            // 跳回循环头继续执行
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
