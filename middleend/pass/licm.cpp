#include <middleend/pass/licm.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/loop_info.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/licm_visitor.h>
#include <middleend/visitor/utils/operand_replace_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <deque>

namespace ME
{
    void LICMPass::runOnModule(Module& module)
    {
        collectImmutableGlobals(module);
        for (auto* function : module.functions) runOnFunctionImpl(*function);
    }

    void LICMPass::runOnFunction(Function& function) { runOnFunctionImpl(function); }

    void LICMPass::runOnFunctionImpl(Function& function)
    {
        if (!function.funcDef) return;
        if (function.blocks.empty()) return;

        auto* cfg = Analysis::AM.get<Analysis::CFG>(function);
        auto* dom = Analysis::AM.get<Analysis::DomInfo>(function);
        if (!cfg || !dom) return;
        const auto& imm_dom = dom->getImmDom();

        // 使用 LoopInfo 分析获取循环信息
        auto* loopInfo = Analysis::AM.get<Analysis::LoopInfo>(function);
        if (!loopInfo || loopInfo->getNumLoops() == 0) return;

        // 先建立 def-use 索引，便于后续判断不变量
        std::unordered_map<size_t, Instruction*>    regDefs;
        std::unordered_map<size_t, size_t>          regDefBlock;
        std::unordered_map<Instruction*, size_t>    instBlock;
        std::map<size_t, std::vector<Instruction*>> userMap;
        buildDefUseMaps(function, regDefs, regDefBlock, instBlock, userMap);
        bool changed = false;

        // 遍历所有循环
        for (auto& loopPtr : loopInfo->getAllLoops())
        {
            Analysis::Loop& loop = *loopPtr;

            std::set<Operand*> loopStoreGlobals;
            bool               loopHasCall = false;
            collectLoopEffects(function, loop, loopStoreGlobals, loopHasCall);

            // 循环内存在内存/调用时，仅提升头块的标量不变量
            bool                  restrictHeader = false;
            LICMMemoryLikeVisitor memoryVisitor;
            for (size_t blockId : loop.blocks)
            {
                Block* block = function.getBlock(blockId);
                if (!block) continue;
                for (auto* inst : block->insts)
                {
                    if (!inst) continue;
                    if (apply(memoryVisitor, *inst))
                    {
                        restrictHeader = true;
                        break;
                    }
                }
                if (restrictHeader) break;
            }

            std::set<Instruction*> invariantInsts;
            std::set<size_t>       invariantRegs;
            collectInvariantInsts(function,
                loop,
                regDefBlock,
                userMap,
                instBlock,
                imm_dom,
                restrictHeader,
                loopStoreGlobals,
                loopHasCall,
                invariantInsts,
                invariantRegs);
            if (invariantInsts.empty()) continue;

            bool   cfgChanged = false;
            Block* preheader = getOrCreatePreheader(function, cfg, loop, cfgChanged);
            if (!preheader) continue;
            if (cfgChanged) changed = true;

            std::vector<Instruction*> hoistOrder;
            buildHoistOrder(function, loop, invariantInsts, hoistOrder);
            if (hoistOrder.empty()) continue;

            // 将不变量指令移动到 preheader
            hoistInstructions(preheader, hoistOrder, instBlock, regDefBlock, function, loop, imm_dom);
            changed = true;
        }

        if (changed) Analysis::AM.invalidate(function);
    }

    void LICMPass::collectImmutableGlobals(Module& module)
    {
        immutableGlobals.clear();
        for (auto* glb : module.globalVars)
        {
            if (glb) immutableGlobals.insert(glb->name);
        }

        std::unordered_set<std::string> definedFuncs;
        for (auto* function : module.functions)
        {
            if (function && function->funcDef) definedFuncs.insert(function->funcDef->funcName);
        }

        LICMGlobalStoreVisitor storeVisitor;
        for (auto* function : module.functions)
        {
            if (!function) continue;
            for (auto& [id, block] : function->blocks)
            {
                for (auto* inst : block->insts)
                {
                    if (!inst) continue;
                    Operand* globalOp = apply(storeVisitor, *inst);
                    if (!globalOp) continue;
                    if (globalOp->getType() != OperandType::GLOBAL) continue;
                    auto* g = static_cast<GlobalOperand*>(globalOp);
                    immutableGlobals.erase(g->name);
                    continue;
                }
            }
        }

        // 若存在调用未知外部函数，保守地认为所有全局变量可被修改
        for (auto* function : module.functions)
        {
            if (!function) continue;
            for (auto& [id, block] : function->blocks)
            {
                for (auto* inst : block->insts)
                {
                    auto* call = dynamic_cast<CallInst*>(inst);
                    if (!call) continue;
                    if (definedFuncs.find(call->funcName) != definedFuncs.end()) continue;
                    immutableGlobals.clear();
                    return;
                }
            }
        }
    }

    bool LICMPass::dominates(int dom, int node, const std::vector<int>& imm_dom) const
    {
        if (dom == node) return true;
        if (dom < 0 || node < 0) return false;
        if (static_cast<size_t>(dom) >= imm_dom.size() || static_cast<size_t>(node) >= imm_dom.size()) return false;

        int cur = node;
        while (cur >= 0 && static_cast<size_t>(cur) < imm_dom.size())
        {
            int parent = imm_dom[cur];
            if (parent == dom) return true;
            if (parent == cur) break;
            cur = parent;
        }
        return false;
    }

    bool LICMPass::dominatesAllLatches(size_t blockId, const Analysis::Loop& loop, const std::vector<int>& imm_dom) const
    {
        if (loop.latches.empty()) return true;
        for (size_t latchId : loop.latches)
        {
            if (!dominates(static_cast<int>(blockId), static_cast<int>(latchId), imm_dom)) return false;
        }
        return true;
    }

    Block* LICMPass::getOrCreatePreheader(Function& function, Analysis::CFG* cfg, Analysis::Loop& loop, bool& cfgChanged)
    {
        size_t headerId = loop.header;
        if (headerId >= cfg->invG_id.size()) return nullptr;

        std::set<size_t> predsOutside;
        for (size_t pred : cfg->invG_id[headerId])
        {
            if (!loop.contains(pred)) predsOutside.insert(pred);
        }

        if (predsOutside.empty()) return nullptr;

        if (predsOutside.size() == 1)
        {
            size_t predId = *predsOutside.begin();
            if (isSingleSuccToHeader(cfg, predId, headerId))
            {
                Block* preheader = function.getBlock(predId);
                return preheader;
            }
        }

        Block* preheader = function.createBlock();
        preheader->setComment("licm.preheader");
        preheader->insts.push_back(new BrUncondInst(getLabelOperand(headerId)));

        // 修改外部前驱的跳转目标
        redirectPredsToPreheader(function, predsOutside, headerId, preheader->blockId);
        updateHeaderPhis(function, function.getBlock(headerId), predsOutside, preheader->blockId);
        cfgChanged = true;

        return preheader;
    }

    void LICMPass::redirectPredsToPreheader(
        Function& function, const std::set<size_t>& preds, size_t headerId, size_t preheaderId)
    {
        Operand*                 oldLabel = getLabelOperand(headerId);
        Operand*                 newLabel = getLabelOperand(preheaderId);
        LICMBranchReplaceVisitor visitor;

        for (size_t predId : preds)
        {
            Block* pred = function.getBlock(predId);
            if (!pred || pred->insts.empty()) continue;
            Instruction* termInst = pred->insts.back();
            apply(visitor, *termInst, oldLabel, newLabel);
        }
    }

    void LICMPass::updateHeaderPhis(
        Function& function, Block* header, const std::set<size_t>& predsOutside, size_t preheaderId)
    {
        if (!header) return;
        Operand* newLabel  = getLabelOperand(preheaderId);
        Block*   preheader = function.getBlock(preheaderId);
        if (!preheader) return;

        std::vector<Instruction*> preheaderPhis;
        for (auto* inst : header->insts)
        {
            auto* phi = dynamic_cast<PhiInst*>(inst);
            if (!phi) continue;

            std::vector<std::pair<Operand*, Operand*>> moved;
            for (size_t predId : predsOutside)
            {
                Operand* oldLabel = getLabelOperand(predId);
                auto     it       = phi->incomingVals.find(oldLabel);
                if (it == phi->incomingVals.end()) continue;
                moved.emplace_back(oldLabel, it->second);
            }

            if (moved.empty()) continue;
            for (auto& item : moved) phi->incomingVals.erase(item.first);

            if (predsOutside.size() == 1)
            {
                phi->addIncoming(moved.front().second, newLabel);
                continue;
            }

            Operand* newRes = getRegOperand(function.getNewRegId());
            auto*    newPhi = new PhiInst(phi->dt, newRes);
            for (auto& item : moved) newPhi->addIncoming(item.second, item.first);
            preheaderPhis.push_back(newPhi);
            phi->addIncoming(newRes, newLabel);
        }

        if (preheaderPhis.empty()) return;

        Instruction* terminator = nullptr;
        if (!preheader->insts.empty() && preheader->insts.back()->isTerminator())
        {
            terminator = preheader->insts.back();
            preheader->insts.pop_back();
        }

        for (auto* inst : preheaderPhis) preheader->insts.push_back(inst);
        if (terminator) preheader->insts.push_back(terminator);
    }

    bool LICMPass::isSingleSuccToHeader(Analysis::CFG* cfg, size_t predId, size_t headerId) const
    {
        if (predId >= cfg->G_id.size()) return false;
        const auto& succs = cfg->G_id[predId];
        if (succs.size() != 1) return false;
        return succs.front() == headerId;
    }

    void LICMPass::buildDefUseMaps(Function& function, std::unordered_map<size_t, Instruction*>& regDefs,
        std::unordered_map<size_t, size_t>& regDefBlock, std::unordered_map<Instruction*, size_t>& instBlock,
        std::map<size_t, std::vector<Instruction*>>& userMap)
    {
        UserCollector userCollector;

        for (auto& [id, block] : function.blocks)
        {
            for (auto* inst : block->insts)
            {
                instBlock[inst] = id;

                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg != 0)
                {
                    regDefs[defReg]     = inst;
                    regDefBlock[defReg] = id;
                }

                apply(userCollector, *inst);
            }
        }

        userMap = userCollector.userMap;
    }

    bool LICMPass::isLoopInvariantOperand(size_t reg, const Analysis::Loop& loop,
        const std::unordered_map<size_t, size_t>& regDefBlock, const std::set<size_t>& invariantRegs) const
    {
        if (invariantRegs.find(reg) != invariantRegs.end()) return true;

        auto defIt = regDefBlock.find(reg);
        if (defIt == regDefBlock.end()) return true;
        return !loop.contains(defIt->second);
    }

    bool LICMPass::areUsesInsideLoop(size_t defReg, const Analysis::Loop& loop,
        const std::map<size_t, std::vector<Instruction*>>& userMap,
        const std::unordered_map<Instruction*, size_t>&    instBlock) const
    {
        auto it = userMap.find(defReg);
        if (it == userMap.end()) return true;

        for (auto* userInst : it->second)
        {
            auto blockIt = instBlock.find(userInst);
            if (blockIt == instBlock.end()) continue;
            // 使用出现在循环外也是允许的，预头块定义能支配后续出口
        }
        return true;
    }

    bool LICMPass::isInvariantInst(Instruction* inst, const Analysis::Loop& loop,
        const std::unordered_map<size_t, size_t>& regDefBlock, const std::set<size_t>& invariantRegs,
        const std::map<size_t, std::vector<Instruction*>>& userMap,
        const std::unordered_map<Instruction*, size_t>& instBlock, const std::vector<int>& imm_dom,
        const std::set<Operand*>& loopStoreGlobals, bool loopHasCall) const
    {
        if (!inst) return false;

        bool                  isInvariantLoad = false;
        LICMGlobalLoadVisitor loadVisitor;
        Operand*              globalOp = apply(loadVisitor, *inst);
        if (globalOp && loopStoreGlobals.find(globalOp) == loopStoreGlobals.end())
        {
            bool allowAcrossCall = false;
            if (globalOp->getType() == OperandType::GLOBAL)
            {
                const auto* g   = static_cast<const GlobalOperand*>(globalOp);
                allowAcrossCall = immutableGlobals.find(g->name) != immutableGlobals.end();
            }
            if (!loopHasCall || allowAcrossCall) isInvariantLoad = true;
        }

        // 仅外提无副作用的标量指令
        LICMScalarVisitor scalarVisitor;
        if (!isInvariantLoad && !apply(scalarVisitor, *inst)) return false;

        DefCollector defCollector;
        apply(defCollector, *inst);
        size_t defReg = defCollector.getResult();
        if (defReg == 0) return false;
        if (!areUsesInsideLoop(defReg, loop, userMap, instBlock)) { return false; }

        auto blockIt = instBlock.find(inst);
        if (blockIt == instBlock.end()) return false;
        if (!dominatesAllLatches(blockIt->second, loop, imm_dom))
        {
            // 条件块中的指令仅在可安全提前执行时才允许外提
            LICMSafeSpecVisitor safeVisitor;
            if (!apply(safeVisitor, *inst))
            {
                auto* arith = dynamic_cast<ArithmeticInst*>(inst);
                if (!arith) return false;
                if (arith->opcode != Operator::DIV && arith->opcode != Operator::MOD && arith->opcode != Operator::FDIV)
                {
                    return false;
                }
            }
        }

        std::map<size_t, int> uses;
        UseCollector          useCollector(uses);
        apply(useCollector, *inst);

        for (auto& [reg, count] : uses)
        {
            if (!isLoopInvariantOperand(reg, loop, regDefBlock, invariantRegs)) return false;
        }
        return true;
    }

    void LICMPass::collectLoopEffects(
        Function& function, const Analysis::Loop& loop, std::set<Operand*>& loopStoreGlobals, bool& loopHasCall) const
    {
        loopStoreGlobals.clear();
        loopHasCall = false;

        LICMGlobalStoreVisitor storeVisitor;
        LICMCallVisitor        callVisitor;

        for (size_t blockId : loop.blocks)
        {
            Block* block = function.getBlock(blockId);
            if (!block) continue;
            for (auto* inst : block->insts)
            {
                if (!inst) continue;
                if (apply(callVisitor, *inst)) loopHasCall = true;
                Operand* globalOp = apply(storeVisitor, *inst);
                if (globalOp) loopStoreGlobals.insert(globalOp);
            }
        }
    }

    void LICMPass::collectInvariantInsts(Function& function, const Analysis::Loop& loop,
        const std::unordered_map<size_t, size_t>&          regDefBlock,
        const std::map<size_t, std::vector<Instruction*>>& userMap,
        const std::unordered_map<Instruction*, size_t>& instBlock, const std::vector<int>& imm_dom, bool restrictHeader,
        const std::set<Operand*>& loopStoreGlobals, bool loopHasCall, std::set<Instruction*>& invariantInsts,
        std::set<size_t>& invariantRegs)
    {
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (size_t blockId : loop.blocks)
            {
                Block* block = function.getBlock(blockId);
                if (!block) continue;
                for (auto* inst : block->insts)
                {
                    if (restrictHeader && blockId != loop.header && !dominatesAllLatches(blockId, loop, imm_dom))
                    {
                        LICMSafeSpecVisitor safeVisitor;
                        if (!apply(safeVisitor, *inst)) continue;
                    }
                    if (invariantInsts.find(inst) != invariantInsts.end()) continue;

                    if (!isInvariantInst(inst,
                            loop,
                            regDefBlock,
                            invariantRegs,
                            userMap,
                            instBlock,
                            imm_dom,
                            loopStoreGlobals,
                            loopHasCall))
                        continue;

                    DefCollector defCollector;
                    apply(defCollector, *inst);
                    size_t defReg = defCollector.getResult();

                    invariantInsts.insert(inst);
                    invariantRegs.insert(defReg);
                    changed = true;
                }
            }
        }
    }

    void LICMPass::buildHoistOrder(Function& function, const Analysis::Loop& loop,
        const std::set<Instruction*>& invariantInsts, std::vector<Instruction*>& hoistOrder)
    {
        std::unordered_map<Instruction*, size_t> instIndex;
        size_t                                   index = 0;
        for (size_t blockId : loop.blocks)
        {
            Block* block = function.getBlock(blockId);
            if (!block) continue;
            for (auto* inst : block->insts)
            {
                if (invariantInsts.find(inst) != invariantInsts.end()) instIndex[inst] = index++;
            }
        }

        std::unordered_map<size_t, Instruction*> regToInst;
        for (auto* inst : invariantInsts)
        {
            DefCollector defCollector;
            apply(defCollector, *inst);
            size_t defReg = defCollector.getResult();
            if (defReg != 0) regToInst[defReg] = inst;
        }

        std::unordered_map<Instruction*, std::vector<Instruction*>> edges;
        std::unordered_map<Instruction*, int>                       indegree;
        for (auto* inst : invariantInsts) indegree[inst] = 0;

        for (auto* inst : invariantInsts)
        {
            std::map<size_t, int> uses;
            UseCollector          useCollector(uses);
            apply(useCollector, *inst);

            for (auto& [reg, count] : uses)
            {
                auto defIt = regToInst.find(reg);
                if (defIt == regToInst.end()) continue;
                Instruction* dep = defIt->second;
                edges[dep].push_back(inst);
                indegree[inst] += 1;
            }
        }

        std::set<std::pair<size_t, Instruction*>> ready;
        for (auto& [inst, deg] : indegree)
        {
            if (deg != 0) continue;
            size_t idx = instIndex[inst];
            ready.insert({idx, inst});
        }

        while (!ready.empty())
        {
            auto         it   = ready.begin();
            Instruction* inst = it->second;
            ready.erase(it);
            hoistOrder.push_back(inst);

            for (auto* succ : edges[inst])
            {
                indegree[succ] -= 1;
                if (indegree[succ] == 0)
                {
                    size_t idx = instIndex[succ];
                    ready.insert({idx, succ});
                }
            }
        }
    }

    void LICMPass::removeInstFromBlock(Block* block, Instruction* inst)
    {
        if (!block || !inst) return;
        for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
        {
            if (*it != inst) continue;
            block->insts.erase(it);
            return;
        }
    }

    void LICMPass::hoistInstructions(Block* preheader, const std::vector<Instruction*>& hoistOrder,
        std::unordered_map<Instruction*, size_t>& instBlock, std::unordered_map<size_t, size_t>& regDefBlock,
        Function& function, const Analysis::Loop& loop, const std::vector<int>& imm_dom)
    {
        if (!preheader) return;

        std::vector<Instruction*>                unsafeInsts;
        std::unordered_set<Instruction*>         unsafeSet;
        std::set<size_t>                         unsafeRegs;
        std::unordered_map<Instruction*, size_t> defRegs;
        for (auto* inst : hoistOrder)
        {
            DefCollector defCollector;
            apply(defCollector, *inst);
            size_t defReg = defCollector.getResult();
            defRegs[inst] = defReg;

            bool  needsGuard = false;
            auto* arith      = dynamic_cast<ArithmeticInst*>(inst);
            if (arith &&
                (arith->opcode == Operator::DIV || arith->opcode == Operator::MOD || arith->opcode == Operator::FDIV))
            {
                auto blockIt = instBlock.find(inst);
                if (blockIt != instBlock.end() && !dominatesAllLatches(blockIt->second, loop, imm_dom))
                {
                    LICMSafeSpecVisitor safeVisitor;
                    if (!apply(safeVisitor, *inst)) needsGuard = true;
                }
            }

            if (needsGuard)
            {
                unsafeInsts.push_back(inst);
                unsafeSet.insert(inst);
                if (defReg != 0) unsafeRegs.insert(defReg);
            }
        }

        std::vector<Instruction*> preGuardInsts;
        std::vector<Instruction*> postGuardInsts;
        std::set<size_t>          guardedRegs = unsafeRegs;
        for (auto* inst : hoistOrder)
        {
            if (unsafeSet.find(inst) != unsafeSet.end()) continue;

            std::map<size_t, int> uses;
            UseCollector          useCollector(uses);
            apply(useCollector, *inst);

            bool dependsOnGuard = false;
            for (auto& [reg, count] : uses)
            {
                if (guardedRegs.find(reg) != guardedRegs.end())
                {
                    dependsOnGuard = true;
                    break;
                }
            }

            if (dependsOnGuard)
            {
                postGuardInsts.push_back(inst);
                size_t defReg = defRegs[inst];
                if (defReg != 0) guardedRegs.insert(defReg);
            }
            else { preGuardInsts.push_back(inst); }
        }

        // 先从原块中移除，避免重复引用
        for (auto* inst : hoistOrder)
        {
            auto it = instBlock.find(inst);
            if (it == instBlock.end()) continue;
            Block* fromBlock = function.getBlock(it->second);
            removeInstFromBlock(fromBlock, inst);
            instBlock[inst] = preheader->blockId;
            size_t defReg   = defRegs[inst];
            if (defReg != 0) regDefBlock[defReg] = preheader->blockId;
        }

        // 将指令插入到 preheader 的终结指令之前
        Instruction* terminator  = nullptr;
        Operand*     headerLabel = getLabelOperand(loop.header);
        if (!preheader->insts.empty() && preheader->insts.back()->isTerminator())
        {
            terminator = preheader->insts.back();
            preheader->insts.pop_back();

            auto* br = dynamic_cast<BrUncondInst*>(terminator);
            if (br) headerLabel = br->target;
        }

        for (auto* inst : preGuardInsts) preheader->insts.push_back(inst);
        if (unsafeInsts.empty())
        {
            for (auto* inst : postGuardInsts) preheader->insts.push_back(inst);
            if (terminator) preheader->insts.push_back(terminator);
            return;
        }
        if (terminator) delete terminator;

        // 带条件守卫的外提：遇到可能除零的运算时，在 preheader 后插入分支保护
        Block*                                   current = preheader;
        std::unordered_map<size_t, Operand*>     replaceRegs;
        std::unordered_map<Instruction*, size_t> finalBlock;
        for (auto* inst : preGuardInsts) finalBlock[inst] = preheader->blockId;

        for (auto* inst : unsafeInsts)
        {
            auto* arith = dynamic_cast<ArithmeticInst*>(inst);
            if (!arith) continue;

            Operand* oldRes = arith->res;
            Operand* divRes = getRegOperand(function.getNewRegId());
            arith->res      = divRes;

            Operand* zero = nullptr;
            if (arith->dt == DataType::F32)
                zero = getImmeF32Operand(0.0f);
            else
                zero = getImmeI32Operand(0);

            Operand*     cmpRes  = getRegOperand(function.getNewRegId());
            Instruction* cmpInst = nullptr;
            if (arith->dt == DataType::F32)
            {
                cmpInst = new FcmpInst(DataType::F32, FCmpOp::ONE, arith->rhs, zero, cmpRes);
            }
            else { cmpInst = new IcmpInst(DataType::I32, ICmpOp::NE, arith->rhs, zero, cmpRes); }

            Block* thenBlock  = function.createBlock();
            Block* elseBlock  = function.createBlock();
            Block* mergeBlock = function.createBlock();

            current->insts.push_back(cmpInst);
            current->insts.push_back(
                new BrCondInst(cmpRes, getLabelOperand(thenBlock->blockId), getLabelOperand(elseBlock->blockId)));

            thenBlock->insts.push_back(inst);
            thenBlock->insts.push_back(new BrUncondInst(getLabelOperand(mergeBlock->blockId)));

            elseBlock->insts.push_back(new BrUncondInst(getLabelOperand(mergeBlock->blockId)));

            Operand* phiRes = getRegOperand(function.getNewRegId());
            auto*    phi    = new PhiInst(arith->dt, phiRes);
            phi->addIncoming(divRes, getLabelOperand(thenBlock->blockId));
            phi->addIncoming(zero, getLabelOperand(elseBlock->blockId));
            mergeBlock->insts.push_back(phi);

            if (oldRes && oldRes->getType() == OperandType::REG)
            {
                replaceRegs[oldRes->getRegNum()] = phiRes;
            }

            finalBlock[inst] = thenBlock->blockId;
            current          = mergeBlock;
        }

        for (auto* inst : postGuardInsts)
        {
            current->insts.push_back(inst);
            finalBlock[inst] = current->blockId;
        }
        current->insts.push_back(new BrUncondInst(headerLabel));

        if (!replaceRegs.empty())
        {
            OperandReplaceVisitor replaceVisitor(replaceRegs);
            for (size_t blockId : loop.blocks)
            {
                Block* block = function.getBlock(blockId);
                if (!block) continue;
                for (auto* inst : block->insts) { apply(replaceVisitor, *inst); }
            }
            for (auto* inst : postGuardInsts) { apply(replaceVisitor, *inst); }
        }

        Operand*              oldLabel = getLabelOperand(preheader->blockId);
        Operand*              newLabel = getLabelOperand(current->blockId);
        LICMPhiReplaceVisitor phiReplace;
        Block*                header = function.getBlock(loop.header);
        if (header && newLabel != oldLabel)
        {
            for (auto* inst : header->insts) { apply(phiReplace, *inst, oldLabel, newLabel); }
        }

        for (auto& [inst, blockId] : finalBlock)
        {
            instBlock[inst] = blockId;
            size_t defReg   = defRegs[inst];
            if (defReg != 0) regDefBlock[defReg] = blockId;
        }
    }

}  // namespace ME
