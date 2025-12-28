#include <middleend/pass/adce.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/postdominfo.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/module/ir_operand.h>
#include <queue>
#include <algorithm>

namespace ME
{
    // 辅助函数：提取基本块的后继
    static std::vector<size_t> getSuccessors(Block* block)
    {
        // 提取基本块的后继
        std::vector<size_t> succs;
        if (!block || block->insts.empty()) return succs;

        Instruction* term = block->insts.back();
        if (term->opcode == Operator::BR_UNCOND)
        {
            auto* br = dynamic_cast<BrUncondInst*>(term);
            if (br->target) succs.push_back(br->target->getLabelNum());
        }
        else if (term->opcode == Operator::BR_COND)
        {
            auto* br = dynamic_cast<BrCondInst*>(term);
            if (br->trueTar) succs.push_back(br->trueTar->getLabelNum());
            if (br->falseTar) succs.push_back(br->falseTar->getLabelNum());
        }
        return succs;
    }

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
        // BFS
        while (!q.empty())
        {
            size_t currId = q.front();
            q.pop();

            auto itBlock = function.blocks.find(currId);
            if (itBlock == function.blocks.end()) continue;

            // 遍历所有后继块
            std::vector<size_t> succs = getSuccessors(itBlock->second);
            for (size_t succId : succs)
            {
                if (reachable.find(succId) == reachable.end())
                {
                    // 标记为可达
                    reachable.insert(succId);
                    q.push(succId);
                }
            }
        }

        // 2. 处理不可达块对可达块 Phi 节点的影响
        for (auto& [id, block] : function.blocks)
        {
            if (reachable.count(id)) continue;  // 可达块跳过

            // 这是一个不可达块，找到它的后继
            std::vector<size_t> succs = getSuccessors(block);

            for (size_t succId : succs)
            {
                if (reachable.count(succId))
                {
                    // 如果后继是可达的，需要更新其 Phi 节点
                    auto succIt = function.blocks.find(succId);
                    if (succIt == function.blocks.end()) continue;
                    Block* succBlock = succIt->second;
                    if (!succBlock) continue;

                    for (auto* inst : succBlock->insts)
                    {
                        if (inst->opcode == Operator::PHI)
                        {
                            auto* phi     = dynamic_cast<PhiInst*>(inst);
                            auto* labelOp = OperandFactory::getInstance().getLabelOperand(id);
                            // 从 Phi 节点中移除该不可达块的输入
                            phi->incomingVals.erase(labelOp);
                        }
                        else { break; }
                    }
                }
            }
        }
    }

    bool ADCEPass::isSideEffect(Instruction* inst)
    {
        switch (inst->opcode)
        {
            case Operator::STORE:  // 存储指令
            case Operator::CALL:   // 函数调用可能有副作用
            case Operator::RET:    // 返回指令
                return true;
            default: return false;
        }
    }

    void ADCEPass::markLive(Function& function)
    {
        Analysis::Manager::getInstance().invalidate(function);

        // 获取控制流图 CFG 和后支配信息
        auto* cfg         = Analysis::Manager::getInstance().get<Analysis::CFG>(function);
        auto* postDomInfo = Analysis::Manager::getInstance().get<Analysis::PostDomInfo>(function);

        numBlocks       = cfg->G_id.size();
        postImmDom      = postDomInfo->getImmPostDom();       // 获取后支配树的直接支配者数组
        const auto& pdf = postDomInfo->getPostDomFrontier();  // 获取后支配边界数组

        // 构建寄存器定义映射，寄存器 -> 定义寄存器值的指令
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

        std::queue<Instruction*>       worklist;  // 存储待处理的活跃指令
        std::map<Instruction*, size_t> instToBlock;

        // 标记所有有副作用的指令为活跃，CFG已经移除了不可达块
        for (auto& [id, block] : function.blocks)
        {
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

        // 传播活跃性
        std::map<size_t, int> dummyMap;  // 收集使用的寄存器
        while (!worklist.empty())
        {
            Instruction* inst = worklist.front();
            worklist.pop();

            // 1. 操作数活跃性：标记定义使用操作数的指令为活跃
            dummyMap.clear();
            UseCollector collector(dummyMap);
            apply(collector, *inst);  // 将该指令进行收集

            for (auto& [reg, _] : dummyMap)
            {
                // 遍历寄存器定义
                if (regDefInst.count(reg))
                {
                    // 如果有定义该寄存器的指令
                    Instruction* defInst = regDefInst[reg];
                    if (liveInsts.find(defInst) == liveInsts.end())
                    {
                        // 如果定义指令尚未标记为活跃，则标记并加入工作列表
                        liveInsts.insert(defInst);
                        worklist.push(defInst);
                    }
                }
            }

            // 2. Phi 节点前驱活跃性
            if (inst->opcode == Operator::PHI)
            {
                // 如果是 Phi 指令，标记其前驱块的终结指令为活跃
                auto* phi = dynamic_cast<PhiInst*>(inst);
                for (auto& [labelOp, val] : phi->incomingVals)
                {
                    // 遍历所有前驱
                    if (labelOp)
                    {
                        // 获取前驱块ID
                        size_t predId = labelOp->getLabelNum();
                        auto   predIt = function.blocks.find(predId);
                        // 前驱不在函数中则跳过
                        if (predIt == function.blocks.end()) continue;

                        // 前驱块为空则跳过
                        Block* predBlock = predIt->second;
                        if (!predBlock) continue;

                        if (!predBlock->insts.empty())
                        {
                            // 将前驱块中的终结指令标记为活跃
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
            size_t blockId = instToBlock[inst];  // 当前块 id
            if (blockId < pdf.size())
            {
                for (int cdBlockId : pdf[blockId])
                {
                    // 遍历所有支配边界块
                    if (cdBlockId >= (int)numBlocks) continue;
                    auto cdIt = function.blocks.find(cdBlockId);
                    if (cdIt == function.blocks.end()) continue;
                    // 获取支配边界块
                    Block* cdBlock = cdIt->second;
                    if (!cdBlock) continue;
                    if (!cdBlock->insts.empty())
                    {
                        // 将支配边界块的终结指令标记为活跃
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

        for (auto& [id, block] : function.blocks)
        {
            std::deque<Instruction*> newInsts;

            for (auto* inst : block->insts)
            {
                // 活跃指令直接保留
                if (liveInsts.count(inst))
                {
                    newInsts.push_back(inst);
                    continue;
                }
                // 死指令：非终结符直接删除
                if (!inst->isTerminator())
                {
                    delete inst;
                    changed = true;
                    continue;
                }
                // 如果一个节点x是不活跃的，那么说x到anti_dom(x)的这些节点一定都不是活跃的
                // 死终结符：寻找活跃的后支配块作为跳转目标，-1表示没有找到
                int targetId = postImmDom[id];

                // 沿着后支配树向上找到第一个活跃块
                while (targetId != -1 && targetId < (int)numBlocks)
                {
                    auto bIt = function.blocks.find(targetId);
                    if (bIt == function.blocks.end() || !bIt->second) break;

                    // 检查该块是否有活跃指令
                    bool isLive = false;
                    for (auto* i : bIt->second->insts)
                    {
                        if (liveInsts.count(i))
                        {
                            isLive = true;
                            break;
                        }
                    }
                    if (isLive) break;

                    // 没有活活跃块 继续向后寻找，-1表示没找到
                    targetId = (targetId < (int)postImmDom.size()) ? postImmDom[targetId] : -1;
                }

                // 情况1: 找到活跃后支配块，重定向跳转
                if (targetId != -1 && targetId < (int)numBlocks)
                {
                    // 如果已经跳转到正确目标，保留原指令
                    if (inst->opcode == Operator::BR_UNCOND)
                    {
                        auto* br = dynamic_cast<BrUncondInst*>(inst);

                        if ((int)br->target->getLabelNum() == targetId)
                        {
                            newInsts.push_back(inst);  // 已经跳转到正确目标
                            continue;
                        }
                    }

                    // 否则就，创建新的无条件跳转
                    auto* newBr = new BrUncondInst(OperandFactory::getInstance().getLabelOperand(targetId));

                    // 更新目标块的 Phi 节点
                    if (auto it = function.blocks.find(targetId); it != function.blocks.end() && it->second)
                    {
                        auto* currentLabel = OperandFactory::getInstance().getLabelOperand(id);
                        for (auto* targetInst : it->second->insts)
                        {
                            // 遍历目标块的指令，直到非Phi指令为止
                            if (targetInst->opcode != Operator::PHI) break;
                            auto* phi = dynamic_cast<PhiInst*>(targetInst);
                            if (phi->incomingVals.find(currentLabel) == phi->incomingVals.end())
                            {
                                // 如果当前块不在Phi的前驱中，添加一个默认值0
                                phi->addIncoming(OperandFactory::getInstance().getImmeI32Operand(0), currentLabel);
                            }
                        }
                    }
                    delete inst;
                    newInsts.push_back(newBr);
                    changed = true;
                }
                // 情况2: 如果后支配链上没有活跃块，
                // 说明无论选择 true 还是 false 分支，最终都不会到达有副作用的代码
                else if (inst->opcode == Operator::BR_COND)
                {
                    auto* br = dynamic_cast<BrCondInst*>(inst);
                    if (auto* target = dynamic_cast<LabelOperand*>(br->trueTar))
                    {  // 创建无条件跳转到 true 目标
                        auto* newBr = new BrUncondInst(target);
                        delete inst;
                        newInsts.push_back(newBr);
                        changed = true;
                    }
                    else
                    {
                        // 如果无法确定目标，保留原指令
                        newInsts.push_back(inst);
                    }
                }
                // 情况3: 其他情况保留原终结符
                else { newInsts.push_back(inst); }
            }

            block->insts = newInsts;
        }
        return changed;
    }
}  // namespace ME
