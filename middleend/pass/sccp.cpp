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
        initialize(function);
        if (function.blocks.empty()) return;
        Block* entry = function.blocks.begin()->second;

        // 入口块入队，启动可达性传播
        if (reachableBlocks.insert(entry->blockId).second) { blockWorklist.push_back(entry); }

        SCCPEvalVisitor evaluator;
        while (!blockWorklist.empty() || !instWorklist.empty())
        {
            if (!blockWorklist.empty())
            {
                Block* block = blockWorklist.front();
                blockWorklist.pop_front();
                // 先处理新可达块，遍历块内所有指令
                SCCPEvalVisitor evaluator;
                for (auto* inst : block->insts) { apply(evaluator, *inst, *this, block); }
            }
            else
            {
                // 再处理受影响的指令，保证增量更新
                Instruction* inst = instWorklist.front();
                instWorklist.pop_front();
                Block* block = instBlockMap[inst];
                if (!block || reachableBlocks.count(block->blockId) == 0) continue;
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
        for (auto& [id, block] : function.blocks)
        {
            // 分支折叠只检查块末尾指令
            if (block->insts.empty()) continue;
            Instruction* term = block->insts.back();
            if (term->opcode == Operator::BR_COND)
            {
                auto* br = static_cast<BrCondInst*>(term);
                if (!br->cond || br->cond->getType() != OperandType::IMMEI32) continue;

                int      condVal = static_cast<ImmeI32Operand*>(br->cond)->value;
                Operand* target  = condVal != 0 ? br->trueTar : br->falseTar;
                Operand* dropped = condVal != 0 ? br->falseTar : br->trueTar;
                if (!target) continue;

                // 删除被折叠掉的边对应的 Phi 来保持 CFG 和 Phi 一致
                if (dropped && dropped->getType() == OperandType::LABEL && currFunc)
                {
                    auto* droppedLabel = static_cast<LabelOperand*>(dropped);
                    Block* dropBlock   = currFunc->getBlock(droppedLabel->lnum);
                    if (dropBlock) removePhiIncoming(dropBlock, getLabelOperand(block->blockId));
                }

                // 用无条件分支替换原条件分支
                auto* newBr = new BrUncondInst(target);
                for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
                {
                    if (*it == br)
                    {
                        *it = newBr;
                        delete br;
                        break;
                    }
                }
            }
        }
        for (auto it = function.blocks.begin(); it != function.blocks.end();)
        {
            size_t blockId = it->first;
            Block* block   = it->second;
            if (reachableBlocks.count(blockId) > 0)
            {
                ++it;
                continue;
            }

            // 删除不可达块前，先修正后继块的 Phi incoming
            if (!block->insts.empty())
            {
                Instruction* term = block->insts.back();
                if (term->opcode == Operator::BR_COND)
                {
                    auto* br = static_cast<BrCondInst*>(term);
                    if (br->trueTar && br->trueTar->getType() == OperandType::LABEL)
                    {
                        auto* label = static_cast<LabelOperand*>(br->trueTar);
                        Block* succ = function.getBlock(label->lnum);
                        if (succ) removePhiIncoming(succ, getLabelOperand(blockId));
                    }
                    if (br->falseTar && br->falseTar->getType() == OperandType::LABEL)
                    {
                        auto* label = static_cast<LabelOperand*>(br->falseTar);
                        Block* succ = function.getBlock(label->lnum);
                        if (succ) removePhiIncoming(succ, getLabelOperand(blockId));
                    }
                }
                else if (term->opcode == Operator::BR_UNCOND)
                {
                    auto* br = static_cast<BrUncondInst*>(term);
                    if (br->target && br->target->getType() == OperandType::LABEL)
                    {
                        auto* label = static_cast<LabelOperand*>(br->target);
                        Block* succ = function.getBlock(label->lnum);
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
        userMap = std::move(collector.userMap);

        // 参数寄存器视为不确定值，避免错误传播
        if (function.funcDef)
        {
            for (auto& arg : function.funcDef->argRegs)
            {
                Operand* op = arg.second;
                if (op && op->getType() == OperandType::REG)
                {
                    LatticeVal overdef;
                    overdef.kind = LatticeKind::OVERDEFINED;
                    overdef.type = DataType::UNK;
                    valueMap[static_cast<RegOperand*>(op)->regNum] = overdef;
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
            phi->incomingVals.erase(label);
        }
    }

}  // namespace ME
