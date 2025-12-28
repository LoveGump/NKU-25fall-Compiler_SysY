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
                // 先处理新可达块，遍历块内所有指令
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
                // 仅处理可达块内的指令，进行格值计算
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

        // Phi 消除与 CFG 简化
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

        // 删除不可达块
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

        // 收集寄存器使用关系，并建立指令到基本块的映射
        // 便于后续通过寄存器值变化快速定位受影响指令
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
