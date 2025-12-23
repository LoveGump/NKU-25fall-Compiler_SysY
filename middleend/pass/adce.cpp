#include <middleend/pass/adce.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <dom_analyzer.h>
#include <middleend/module/ir_operand.h>
#include <queue>
#include <algorithm>
#include <iostream>

namespace ME
{
    void ADCEPass::runOnFunction(Function& function)
    {
        liveInsts.clear();
        postImmDom.clear();

        // 1. 标记活跃指令
        markLive(function);

        // 2. 移除死代码
        removeDeadCode(function);

        // 3. 清理不可达块
        cleanUp(function);
        Analysis::Manager::getInstance().invalidate(function);
        Analysis::Manager::getInstance().get<Analysis::CFG>(function);
    }

    void ADCEPass::cleanUp(Function& function)
    {
        // 1. 计算可达性
        std::set<size_t>   reachable;
        std::queue<size_t> q;

        if (function.blocks.count(0))
        {
            q.push(0);
            reachable.insert(0);
        }

        while (!q.empty())
        {
            size_t currId = q.front();
            q.pop();

            Block* currBlock = function.blocks[currId];
            if (currBlock->insts.empty()) continue;

            Instruction*        term = currBlock->insts.back();
            std::vector<size_t> succs;

            if (term->opcode == Operator::BR_UNCOND)
            {
                auto* br    = dynamic_cast<BrUncondInst*>(term);
                auto* label = dynamic_cast<LabelOperand*>(br->target);
                if (label) succs.push_back(label->lnum);
            }
            else if (term->opcode == Operator::BR_COND)
            {
                auto* br = dynamic_cast<BrCondInst*>(term);
                auto* l1 = dynamic_cast<LabelOperand*>(br->trueTar);
                auto* l2 = dynamic_cast<LabelOperand*>(br->falseTar);
                if (l1) succs.push_back(l1->lnum);
                if (l2) succs.push_back(l2->lnum);
            }

            for (size_t succId : succs)
            {
                if (reachable.find(succId) == reachable.end())
                {
                    reachable.insert(succId);
                    q.push(succId);
                }
            }
        }

        // 2. 处理不可达块对可达块 Phi 节点的影响
        for (auto& [id, block] : function.blocks)
        {
            if (reachable.count(id)) continue;

            // 这是一个不可达块
            // 找到它的后继
            if (block->insts.empty()) continue;
            Instruction*        term = block->insts.back();
            std::vector<size_t> succs;
            if (term->opcode == Operator::BR_UNCOND)
            {
                auto* br    = dynamic_cast<BrUncondInst*>(term);
                auto* label = dynamic_cast<LabelOperand*>(br->target);
                if (label) succs.push_back(label->lnum);
            }
            else if (term->opcode == Operator::BR_COND)
            {
                auto* br = dynamic_cast<BrCondInst*>(term);
                auto* l1 = dynamic_cast<LabelOperand*>(br->trueTar);
                auto* l2 = dynamic_cast<LabelOperand*>(br->falseTar);
                if (l1) succs.push_back(l1->lnum);
                if (l2) succs.push_back(l2->lnum);
            }

            for (size_t succId : succs)
            {
                if (reachable.count(succId))
                {
                    // 后继是可达的，需要更新其 Phi 节点
                    Block* succBlock = function.blocks[succId];
                    for (auto* inst : succBlock->insts)
                    {
                        if (inst->opcode == Operator::PHI)
                        {
                            auto* phi     = dynamic_cast<PhiInst*>(inst);
                            auto* labelOp = OperandFactory::getInstance().getLabelOperand(id);
                            phi->incomingVals.erase(labelOp);
                        }
                        else
                        {
                            break;
                        }
                    }
                }
            }
        }
    }

    bool ADCEPass::isSideEffect(Instruction* inst)
    {
        switch (inst->opcode)
        {
            case Operator::STORE:
            case Operator::CALL:
            case Operator::RET:
            case Operator::FUNCDECL:
            case Operator::FUNCDEF:
            case Operator::GLOBAL_VAR: return true;
            default: return false;
        }
    }

    Operand* ADCEPass::getConstantValue(Operand* op, std::map<size_t, Instruction*>& regDefInst, int depth)
    {
        if (depth > 3) return nullptr;
        if (op->getType() == OperandType::IMMEI32 || op->getType() == OperandType::IMMEF32) { return op; }
        if (op->getType() == OperandType::REG)
        {
            size_t reg = ((RegOperand*)op)->regNum;
            if (regDefInst.count(reg))
            {
                Instruction* def = regDefInst[reg];
                if (def->opcode == Operator::ADD)
                {
                    auto* bin = (ArithmeticInst*)def;
                    auto* lhs = getConstantValue(bin->lhs, regDefInst, depth + 1);
                    auto* rhs = getConstantValue(bin->rhs, regDefInst, depth + 1);
                    if (lhs && rhs && lhs->getType() == OperandType::IMMEI32 && rhs->getType() == OperandType::IMMEI32)
                    {
                        int l = ((ImmeI32Operand*)lhs)->value;
                        int r = ((ImmeI32Operand*)rhs)->value;
                        return OperandFactory::getInstance().getImmeI32Operand(l + r);
                    }
                }
                else if (def->opcode == Operator::ICMP)
                {
                    auto* icmp = (IcmpInst*)def;
                    auto* lhs  = getConstantValue(icmp->lhs, regDefInst, depth + 1);
                    auto* rhs  = getConstantValue(icmp->rhs, regDefInst, depth + 1);
                    if (lhs && rhs && lhs->getType() == OperandType::IMMEI32 && rhs->getType() == OperandType::IMMEI32)
                    {
                        int  l   = ((ImmeI32Operand*)lhs)->value;
                        int  r   = ((ImmeI32Operand*)rhs)->value;
                        bool res = false;
                        switch (icmp->cond)
                        {
                            case ICmpOp::EQ: res = (l == r); break;
                            case ICmpOp::NE: res = (l != r); break;
                            case ICmpOp::SGT: res = (l > r); break;
                            case ICmpOp::SGE: res = (l >= r); break;
                            case ICmpOp::SLT: res = (l < r); break;
                            case ICmpOp::SLE: res = (l <= r); break;
                            case ICmpOp::UGT: res = ((unsigned)l > (unsigned)r); break;
                            case ICmpOp::UGE: res = ((unsigned)l >= (unsigned)r); break;
                            case ICmpOp::ULT: res = ((unsigned)l < (unsigned)r); break;
                            case ICmpOp::ULE: res = ((unsigned)l <= (unsigned)r); break;
                            default: return nullptr;
                        }
                        return OperandFactory::getInstance().getImmeI32Operand(res ? 1 : 0);
                    }
                }
            }
        }
        return nullptr;
    }

    void ADCEPass::markLive(Function& function)
    {
        Analysis::Manager::getInstance().invalidate(function);
        auto* cfg = Analysis::Manager::getInstance().get<Analysis::CFG>(function);
        numBlocks = cfg->G_id.size();

        std::map<size_t, Instruction*> regDefInst;
        for (auto& [id, block] : function.blocks)
        {
            for (auto* inst : block->insts)
            {
                DefCollector defCollector;
                apply(defCollector, *inst);
                size_t defReg = defCollector.getResult();
                if (defReg != 0) { regDefInst[defReg] = inst; }
            }
        }

        // 构建图用于 DomAnalyzer
        std::vector<std::vector<int>> graph_int(numBlocks);
        for (size_t i = 0; i < numBlocks; ++i)
        {
            // 检查是否是条件分支，并尝试常量折叠
            bool pruned = false;
            if (cfg->id2block.count(i))
            {
                Block*       b    = cfg->id2block[i];
                Instruction* term = b->insts.empty() ? nullptr : b->insts.back();
                if (term && term->opcode == Operator::BR_COND)
                {
                    auto*    br   = dynamic_cast<BrCondInst*>(term);
                    Operand* cond = getConstantValue(br->cond, regDefInst);
                    if (cond && cond->getType() == OperandType::IMMEI32)
                    {
                        int val = ((ImmeI32Operand*)cond)->value;
                        if (val != 0)
                        {
                            // True branch only
                            if (auto* label = dynamic_cast<LabelOperand*>(br->trueTar))
                            {
                                graph_int[i].push_back((int)label->lnum);
                            }
                        }
                        else
                        {
                            // False branch only
                            if (auto* label = dynamic_cast<LabelOperand*>(br->falseTar))
                            {
                                graph_int[i].push_back((int)label->lnum);
                            }
                        }
                        pruned = true;
                    }
                }
            }

            if (!pruned)
            {
                for (size_t succ : cfg->G_id[i]) { graph_int[i].push_back((int)succ); }
            }
        }

        // 计算可达性 (BFS)
        std::vector<bool> reachable(numBlocks, false);
        std::queue<int>   q;
        q.push(0);
        reachable[0] = true;
        while (!q.empty())
        {
            int u = q.front();
            q.pop();
            for (int v : graph_int[u])
            {
                if (!reachable[v])
                {
                    reachable[v] = true;
                    q.push(v);
                }
            }
        }

        // 寻找出口
        std::vector<int> exitPoints;
        for (size_t i = 0; i < numBlocks; ++i)
        {
            if (!reachable[i]) continue;  // 忽略不可达块

            if (graph_int[i].empty())
            {
                // 只有当该 blockId 确实存在于 CFG 中时，才将其视为出口
                // 否则它只是一个不存在的节点（因为 G_id 大小是 maxBlockId + 1）
                if (cfg->id2block.find(i) != cfg->id2block.end()) { exitPoints.push_back((int)i); }
            }
            else
            {
                auto it = function.blocks.find(i);
                if (it != function.blocks.end())
                {
                    Block* b = it->second;
                    if (!b->insts.empty() && b->insts.back()->opcode == Operator::RET) { exitPoints.push_back((int)i); }
                }
            }
        }
        if (exitPoints.empty())
        {
            for (auto& [id, block] : function.blocks)
            {
                if (reachable[id] && !block->insts.empty() && block->insts.back()->opcode == Operator::RET)
                {
                    exitPoints.push_back((int)id);
                }
            }
        }

        DomAnalyzer postDomAnalyzer;
        postDomAnalyzer.solve(graph_int, exitPoints, true);
        postImmDom      = postDomAnalyzer.imm_dom;
        const auto& pdf = postDomAnalyzer.dom_frontier;

        std::queue<Instruction*>       worklist;
        std::map<Instruction*, size_t> instToBlock;

        for (auto& [id, block] : function.blocks)
        {
            if (!reachable[id]) continue;  // 忽略不可达块中的指令

            for (auto* inst : block->insts)
            {
                instToBlock[inst] = id;

                if (isSideEffect(inst))
                {
                    liveInsts.insert(inst);
                    worklist.push(inst);
                }
            }
        }

        std::map<size_t, int> dummyMap;
        while (!worklist.empty())
        {
            Instruction* inst = worklist.front();
            worklist.pop();

            // 1. 操作数活跃性
            dummyMap.clear();
            UseCollector collector(dummyMap);
            apply(collector, *inst);

            for (auto& [reg, _] : dummyMap)
            {
                if (regDefInst.count(reg))
                {
                    Instruction* defInst = regDefInst[reg];
                    if (liveInsts.find(defInst) == liveInsts.end())
                    {
                        liveInsts.insert(defInst);
                        worklist.push(defInst);
                    }
                }
            }

            // 2. Phi 节点前驱活跃性
            if (inst->opcode == Operator::PHI)
            {
                auto* phi = dynamic_cast<PhiInst*>(inst);
                for (auto& [labelOp, val] : phi->incomingVals)
                {
                    auto* label = dynamic_cast<LabelOperand*>(labelOp);
                    if (label)
                    {
                        size_t predId    = label->lnum;
                        Block* predBlock = function.blocks[predId];
                        if (!predBlock->insts.empty())
                        {
                            Instruction* term = predBlock->insts.back();
                            if (term->isTerminator() && liveInsts.find(term) == liveInsts.end())
                            {
                                liveInsts.insert(term);
                                worklist.push(term);
                            }
                        }
                    }
                }
            }

            // 3. 控制依赖活跃性
            size_t blockId = instToBlock[inst];
            if (blockId < pdf.size())
            {
                for (int cdBlockId : pdf[blockId])
                {
                    if (cdBlockId >= (int)numBlocks) continue;

                    Block* cdBlock = function.blocks[cdBlockId];
                    if (!cdBlock->insts.empty())
                    {
                        Instruction* term = cdBlock->insts.back();
                        if (term->isTerminator() && liveInsts.find(term) == liveInsts.end())
                        {
                            liveInsts.insert(term);
                            worklist.push(term);
                        }
                    }
                }
            }
        }
    }

    bool ADCEPass::removeDeadCode(Function& function)
    {
        bool changed = false;

        auto isDeadBlock = [&](size_t blockId) {
            if (function.blocks.find(blockId) == function.blocks.end()) return false;
            Block* b = function.blocks[blockId];
            for (auto* inst : b->insts)
            {
                if (liveInsts.count(inst)) return false;
            }
            return true;
        };

        for (auto& [id, block] : function.blocks)
        {
            auto it = block->insts.begin();
            while (it != block->insts.end())
            {
                Instruction* inst = *it;
                if (liveInsts.find(inst) == liveInsts.end())
                {
                    if (!inst->isTerminator())
                    {
                        delete inst;
                        it      = block->insts.erase(it);
                        changed = true;
                    }
                    else
                    {
                        // 处理死掉的 terminator
                        int ipdomId = -1;
                        if (id < postImmDom.size()) ipdomId = postImmDom[id];

                        // 寻找最近的活跃后支配节点
                        while (ipdomId != -1 && ipdomId < (int)numBlocks && isDeadBlock(ipdomId))
                        {
                            if (ipdomId < (int)postImmDom.size())
                                ipdomId = postImmDom[ipdomId];
                            else
                                ipdomId = -1;
                        }

                        // 如果找到了有效的后支配节点，且不是虚拟出口
                        if (ipdomId != -1 && ipdomId < (int)numBlocks)
                        {
                            bool alreadyJumpToIpdom = false;
                            if (inst->opcode == Operator::BR_UNCOND)
                            {
                                auto* br    = dynamic_cast<BrUncondInst*>(inst);
                                auto* label = dynamic_cast<LabelOperand*>(br->target);
                                if (label && (int)label->lnum == ipdomId) { alreadyJumpToIpdom = true; }
                            }

                            if (!alreadyJumpToIpdom)
                            {
                                auto* targetLabel = OperandFactory::getInstance().getLabelOperand(ipdomId);
                                auto* newBr       = new BrUncondInst(targetLabel);

                                Block* ipdomBlock = function.blocks[ipdomId];
                                for (auto* ipdomInst : ipdomBlock->insts)
                                {
                                    if (ipdomInst->opcode == Operator::PHI)
                                    {
                                        auto* phi          = dynamic_cast<PhiInst*>(ipdomInst);
                                        auto* currentLabel = OperandFactory::getInstance().getLabelOperand(id);
                                        if (phi->incomingVals.find(currentLabel) == phi->incomingVals.end())
                                        {
                                            auto* undefVal = OperandFactory::getInstance().getImmeI32Operand(0);
                                            phi->addIncoming(undefVal, currentLabel);
                                        }
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }

                                delete inst;
                                *it     = newBr;
                                changed = true;
                            }
                        }
                        else if (inst->opcode == Operator::BR_COND)
                        {
                            // 如果没有有效的后支配节点（例如多出口或无限循环），
                            // 但分支指令是死代码，我们可以将其转换为无条件跳转到其中一个分支。
                            // 这样消除了“选择”这个死行为。
                            auto* br = dynamic_cast<BrCondInst*>(inst);
                            // 选择 true 分支作为目标
                            auto* targetLabel = dynamic_cast<LabelOperand*>(br->trueTar);
                            if (targetLabel)
                            {
                                auto* newBr = new BrUncondInst(targetLabel);
                                delete inst;
                                *it     = newBr;
                                changed = true;
                            }
                        }
                        ++it;
                    }
                }
                else
                {
                    ++it;
                }
            }
        }
        return changed;
    }
}  // namespace ME
