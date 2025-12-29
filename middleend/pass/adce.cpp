#include <middleend/pass/adce.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/postdominfo.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/module/ir_operand.h>
#include <queue>
#include <algorithm>

namespace ME
{
    // 提取基本块的后继
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

        // ADCE（Aggressive Dead Code Elimination）：
        // - 先从“必活跃（有副作用）”指令出发做活跃性传播，得到 liveInsts
        // - 再删除不活跃指令，并利用后支配信息把死分支重定向到最近的活跃后支配块
        // - 最后清理不可达块以及因此造成的 Phi 入边

        // 1) 标记活跃指令
        markLive(function);

        // 2) 移除死代码（包含死指令删除 + 死终结符分支修复）
        removeDeadCode(function);

        // 3) 清理不可达块（以及可达块 Phi 中来自不可达前驱的 incoming）
        cleanUp(function);
        Analysis::Manager::getInstance().invalidate(function);
        Analysis::Manager::getInstance().get<Analysis::CFG>(function);
    }

    void ADCEPass::cleanUp(Function& function)
    {
        // 1) 从入口块做 BFS，计算可达块集合
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

        // 2) 不可达块仍可能是可达块的 CFG 前驱：需要从可达块的 Phi 中移除该 incoming
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
        // “必活跃”起点：这些指令即使结果没人用，也不能删（会改变可观察行为）
        switch (inst->opcode)
        {
            case Operator::STORE:  // 存储指令
            case Operator::CALL:   // 函数调用我们默认是有副作用的，完整adce也可以对函数进行分析，分析函数是否有副作用
            case Operator::RET:    // 返回指令
                return true;
            default: return false;
        }
    }

    void ADCEPass::markLive(Function& function)
    {
        Analysis::Manager::getInstance().invalidate     (function);

        // 获取控制流图 CFG 和后支配信息（用于控制依赖传播）
        auto* cfg         = Analysis::Manager::getInstance().get<Analysis::CFG>(function);
        auto* postDomInfo = Analysis::Manager::getInstance().get<Analysis::PostDomInfo>(function);

        numBlocks       = cfg->G_id.size();
        postImmDom      = postDomInfo->getImmPostDom();       // 获取后支配树的直接支配者数组
        const auto& pdf = postDomInfo->getPostDomFrontier();  // 获取后支配边界数组

        // 构建寄存器定义映射：reg -> 定义该 reg 的指令
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

        // 初始化：把所有有副作用的指令加入活跃集合与工作队列
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

        // 活跃性传播（三条规则）：
        // 1) 数据依赖：活跃指令用到的寄存器，其定义指令也必须活跃
        // 2) Phi 前驱：若 Phi 活跃，则所有 incoming 对应前驱块的终结符必须活跃（否则 CFG 结构会被破坏）
        // 3) 控制依赖：若某块里有活跃指令，则其后支配边界(PostDomFrontier)上的终结符必须活跃
        //    直观理解：这些终结符控制了是否会走到该活跃指令（必须保留控制流）
        // 在这里就是程序流上面的分支点

        std::map<size_t, int> dummyMap;  // 收集使用的寄存器 (寄存器号 -> 使用次数)，仅用作占位
        while (!worklist.empty())
        {
            Instruction* inst = worklist.front();
            worklist.pop();

            // 1) 数据依赖传播：标记定义所用寄存器的指令
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

            // 2) Phi 前驱传播：Phi 活跃 => incoming 对应前驱块的 terminator 活跃
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

            // 3) 控制依赖传播：通过 post-dominance frontier 找到控制本块可达性的分支点
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
        // 根据 liveInsts 删除死指令，并修复因此可能破坏的 CFG：
        // - 非终结符死指令：直接删除
        // - 终结符死指令：利用后支配链重定向到最近的“活跃后支配块”（保证仍能到达副作用）
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
                
                // jump/branch这样的terminal的处理。如果一个块的terminal被标记为不活跃的，应当跳到它的后继中第一个活跃的块上
                // 如果一个节点x是不活跃的，那么说x到anti_dom(x)的这些节点一定都不是活跃的
                // https://www.cnblogs.com/lixingyang/p/17728846.html
                // 死终结符：寻找活跃的后支配块作为跳转目标，-1表示没有找到
                int targetId = postImmDom[id];

                // 沿着后支配树向上找到第一个 包含活跃指令 的块
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

                    // 更新目标块的 Phi：如果当前块成为目标块的新前驱，需要补齐 incoming（默认 0）
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

/*
ADCE 流程总结（对应本文件实现）：
1) 构建活跃集合 liveInsts：
   - 起点：STORE/CALL/RET 等有副作用的指令；
   - 传播：数据依赖(寄存器 def) + Phi 前驱终结符 + 控制依赖(后支配边界上的终结符)。
2) 删除死代码：
   - 删除不活跃的非终结符指令；
   - 对死终结符：沿后支配链找到最近的“活跃后支配块”，改写为无条件跳转，并维护目标块 Phi 的 incoming。
3) 清理 CFG：
   - BFS 计算可达块，移除不可达块对可达块 Phi 的 incoming 影响。
*/
