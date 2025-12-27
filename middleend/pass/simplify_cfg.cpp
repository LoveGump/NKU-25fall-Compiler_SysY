#include <middleend/pass/simplify_cfg.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <transfer.h>

namespace ME
{
    void SimplifyCFGPass::runOnFunction(Function& function)
    {
        bool changed = true;
        while (changed)
        {
            changed = false;

            // 找一个可以删除的块
            size_t deleteId = 0;
            size_t targetId = 0;
            for (auto& [id, block] : function.blocks)
            {
                if (id == 0) continue;
                if (block->insts.size() != 1) continue;

                auto* br = dynamic_cast<BrUncondInst*>(block->insts.front());
                if (!br) continue;

                size_t target = br->target->getLabelNum();
                if (target == id) continue;

                Block* targetBlock = function.getBlock(target);
                if (!targetBlock) continue;

                // 检查目标块的PHI是否引用当前块
                bool phiRefersThis = false;
                for (auto* inst : targetBlock->insts)
                {
                    if (auto* phi = dynamic_cast<PhiInst*>(inst))
                    {
                        for (auto& [label, val] : phi->incomingVals)
                        {
                            if (label->getLabelNum() == id)
                            {
                                phiRefersThis = true;
                                break;
                            }
                        }
                        if (phiRefersThis) break;
                    }
                }

                if (!phiRefersThis)
                {
                    deleteId = id;
                    targetId = target;
                    break;
                }
            }

            if (deleteId == 0) break;

            // 更新所有跳转到此块的指令
            for (auto& [id, block] : function.blocks)
            {
                for (auto* inst : block->insts)
                {
                    if (auto* ubr = dynamic_cast<BrUncondInst*>(inst))
                    {
                        if (ubr->target->getLabelNum() == deleteId)
                            ubr->target = getLabelOperand(targetId);
                    }
                    else if (auto* cbr = dynamic_cast<BrCondInst*>(inst))
                    {
                        if (cbr->trueTar->getLabelNum() == deleteId)
                            cbr->trueTar = getLabelOperand(targetId);
                        if (cbr->falseTar->getLabelNum() == deleteId)
                            cbr->falseTar = getLabelOperand(targetId);
                    }
                }
            }

            // 删除块
            auto it = function.blocks.find(deleteId);
            if (it != function.blocks.end())
            {
                delete it->second;
                function.blocks.erase(it);
                changed = true;
            }
        }

        Analysis::AM.invalidate(function);
    }
}  // namespace ME
