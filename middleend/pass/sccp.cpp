// 
#include <middleend/pass/sccp.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/sccp_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ME
{
    void SCCPPass::runOnFunction(Function& function)
    {
        // SCCP（Sparse Conditional Constant Propagation）：
        // 目标：在不遍历全 CFG 全状态的前提下，同时完成
        // - 常量传播：把能确定为常量的寄存器用常量替换
        // - 不可达性传播：利用可达边/可达块信息，把恒真/恒假的分支折叠并删除不可达块
        //
        // 本实现采用“两类工作队列”增量求不动点：
        // - blockWorklist：新变为可达的基本块，需要遍历其全部指令
        // - instWorklist：因为某个寄存器格值变化而受影响的指令，只需增量重算
        //
        // 具体的格值计算与边可达性更新由 SCCPEvalVisitor 驱动，它会回调 SCCPPass 维护的表。

        // 初始化分析状态
        initialize(function);
        if (function.blocks.empty()) return;
        Block* entry = function.blocks.begin()->second;

        // 从入口块开始，标记其为可达块
        if (reachableBlocks.insert(entry->blockId).second)
        {
            // 将入口id插入可达块集合成功，说明是新可达块
            blockWorklist.push_back(entry);
        }

        SCCPEvalVisitor evaluator;
        while (!blockWorklist.empty() || !instWorklist.empty())
        {
            // 当有新可达块或受影响指令时，继续处理
            if (!blockWorklist.empty())
            {
                // 优先处理新可达块
                Block* block = blockWorklist.front();
                blockWorklist.pop_front();
                // 新可达块：遍历块内所有指令，建立/更新寄存器格值、可达边与后继可达性
                for (auto* inst : block->insts)
                {
                    // 遍历指令并进行格值的计算
                    apply(evaluator, *inst, *this, block);
                }
            }
            else
            {
                // 再处理受影响的指令，保证增量更新
                Instruction* inst = instWorklist.front();
                instWorklist.pop_front();

                // 获取指令所在的基本块
                Block* block = instBlockMap[inst];
                if (!block || reachableBlocks.count(block->blockId) == 0) continue;
                // 仅对“可达块内”指令进行增量重算，避免把不可达路径的值错误传播出来
                apply(evaluator, *inst, *this, block);
            }
        }

        SCCPReplaceVisitor replacer;
        for (auto& [id, block] : function.blocks)
        {
            // 仅对可达块做常量替换
            if (reachableBlocks.count(id) == 0) continue;
            for (auto* inst : block->insts) { apply(replacer, *inst, *this); }
        }

        // 结果落地：
        // 1) Phi 消除：若 Phi 结果已被判定为常量，使用点已替换为常量，则删除 Phi
        // 2) 分支折叠：若条件已变为立即数常量，将条件分支改写为无条件分支，并维护被丢弃边的 Phi incoming
        for (auto& [id, block] : function.blocks)
        {
            // 仅对可达块处理
            if (reachableBlocks.count(id) == 0) continue;

            // 1. Phi 消除：如果 Phi 结果是常量，删除该 Phi 指令
            std::deque<Instruction*> newInsts;
            for (auto* inst : block->insts)
            {
                if (inst->opcode == Operator::PHI)
                {
                    auto* phi = static_cast<PhiInst*>(inst);
                    if (phi->res && phi->res->getType() == OperandType::REG)
                    {
                        size_t reg = phi->res->getRegNum();
                        auto   it  = valueMap.find(reg);
                        if (it != valueMap.end() && it->second.kind == LatticeKind::CONST)
                        {
                            // Phi 结果是常量，可以删除，此时使用点已被替换为常量
                            delete inst;
                            continue;
                        }
                    }
                }
                newInsts.push_back(inst);
            }
            // 更新指令列表
            block->insts = newInsts;

            // 2. 条件分支折叠
            if (block->insts.empty()) continue;
            Instruction* term = block->insts.back();
            if (term->opcode == Operator::BR_COND)
            {
                auto* br = static_cast<BrCondInst*>(term);
                if (!br->cond || br->cond->getType() != OperandType::IMMEI32) continue;

                // 获取条件值，决定跳转方向
                int      condVal = static_cast<ImmeI32Operand*>(br->cond)->value;
                Operand* target  = condVal != 0 ? br->trueTar : br->falseTar;
                Operand* dropped = condVal != 0 ? br->falseTar : br->trueTar;
                if (!target) continue;

                // 删除被折叠掉的边对应的 Phi 来保持 CFG 和 Phi 一致
                if (dropped && dropped->getType() == OperandType::LABEL && currFunc)
                {
                    Block* dropBlock = currFunc->getBlock(dropped->getLabelNum());
                    if (dropBlock) removePhiIncoming(dropBlock, getLabelOperand(block->blockId));
                }

                // 用无条件分支替换原条件分支
                auto* newBr         = new BrUncondInst(target);
                block->insts.back() = newBr;
                delete br;
            }
        }

        // 删除不可达块：移除块前先清理其后继块的 Phi incoming（避免悬挂的 label）
        for (auto it = function.blocks.begin(); it != function.blocks.end();)
        {
            size_t blockId = it->first;
            Block* block   = it->second;
            if (reachableBlocks.count(blockId))
            {
                // 可达块，保留
                ++it;
                continue;
            }

            // 删除不可达块前，先修正后继块的 Phi incoming
            if (!block->insts.empty())
            {
                Instruction* term = block->insts.back();
                if (term->opcode == Operator::BR_COND)
                {
                    // 如果是条件分支，处理两个后继块
                    auto* br = static_cast<BrCondInst*>(term);
                    if (br->trueTar && br->trueTar->getType() == OperandType::LABEL)
                    {
                        Block* succ = function.getBlock(br->trueTar->getLabelNum());
                        if (succ) removePhiIncoming(succ, getLabelOperand(blockId));
                    }
                    if (br->falseTar && br->falseTar->getType() == OperandType::LABEL)
                    {
                        Block* succ = function.getBlock(br->falseTar->getLabelNum());
                        if (succ) removePhiIncoming(succ, getLabelOperand(blockId));
                    }
                }
                else if (term->opcode == Operator::BR_UNCOND)
                {
                    auto* br = static_cast<BrUncondInst*>(term);
                    if (br->target && br->target->getType() == OperandType::LABEL)
                    {
                        Block* succ = function.getBlock(br->target->getLabelNum());
                        if (succ) removePhiIncoming(succ, getLabelOperand(blockId));
                    }
                }
            }

            // 真正删除块本体
            delete block;
            it = function.blocks.erase(it);
        }
        Analysis::AM.invalidate(function);
    }

    void SCCPPass::initialize(Function& function)
    {
        // 重置分析状态
        currFunc = &function;
        valueMap.clear();
        userMap.clear();
        instBlockMap.clear();
        reachableBlocks.clear();
        reachableEdges.clear();
        blockWorklist.clear();
        instWorklist.clear();

        // 收集寄存器 use 关系，并建立 inst -> block 映射：
        // - userMap 用于“某寄存器格值变化后，快速把其所有使用点入队(instWorklist)”
        // - instBlockMap 用于从指令反查其所在块，从而判断其是否可达并参与求值
        UserCollector collector;
        for (auto& [id, block] : function.blocks)
        {
            for (auto* inst : block->insts)
            {
                apply(collector, *inst);
                instBlockMap[inst] = block;
            }
        }
        userMap = std::move(collector.userMap);  // 寄存器编号 -> 使用该寄存器的指令列表

        // 参数寄存器视为不确定值，避免错误传播
        if (function.funcDef)
        {
            for (auto& arg : function.funcDef->argRegs)
            {
                Operand* op = arg.second;
                if (op && op->getType() == OperandType::REG)
                {
                    LatticeVal overdef;
                    overdef.kind              = LatticeKind::OVERDEFINED;
                    overdef.type              = DataType::UNK;
                    valueMap[op->getRegNum()] = overdef;
                }
            }
        }
    }

    void SCCPPass::removePhiIncoming(Block* block, Operand* label)
    {
        if (!block || !label) return;
        // Phi 指令在块首连续排列，遇到非 Phi 即停止
        for (auto* inst : block->insts)
        {
            if (inst->opcode != Operator::PHI) break;
            auto* phi = static_cast<PhiInst*>(inst);
            // 移除对应的 incoming 项
            phi->incomingVals.erase(label);
        }
    }

}  // namespace ME

/*
SCCP 流程总结（对应本文件实现）：
1) 初始化：
   - 构建 userMap(reg -> users) 与 instBlockMap(inst -> block)；
   - 初始化格值表 valueMap（参数寄存器直接置为 OVERDEFINED）；
   - 从入口块开始，将其加入 reachableBlocks 与 blockWorklist。
2) 求不动点（两类工作队列）：
   - 处理 blockWorklist：对新可达块遍历所有指令，计算/合并格值，并根据分支结果标记可达边与新可达块；
   - 处理 instWorklist：当寄存器格值变化时，仅重算受影响指令，做增量更新。
3) 落地到 IR：
   - 对可达块做常量替换（ReplaceVisitor）；
   - 删除“结果已为常量”的 Phi；
   - 若条件分支条件变为立即数常量，则折叠为无条件分支，并移除被丢弃边对目标块 Phi 的 incoming。
4) 删除不可达块，并先从其后继块 Phi 中移除 incoming，最后使分析缓存失效。
*/
