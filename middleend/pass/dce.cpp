#include <middleend/pass/dce.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <algorithm>

namespace ME
{
    void DCEPass::runOnFunction(Function& function)
    {
        bool changed = true;
        while (changed) { changed = eliminateDeadCode(function); }
    }

    bool DCEPass::eliminateDeadCode(Function& function)
    {
        std::map<size_t, int> useCounts;
        bool                  changed = false;

        // 1. 收集使用次数
        UseCollector useCollector(useCounts);
        for (auto& [id, block] : function.blocks)
        {
            for (auto inst : block->insts) { apply(useCollector, *inst); }
        }

        // 2. 识别并移除死代码
        DefCollector defCollector;
        for (auto& [id, block] : function.blocks)
        {
            auto it = block->insts.begin();
            while (it != block->insts.end())
            {
                Instruction* inst = *it;
                if (!isSideEffect(inst))
                {
                    apply(defCollector, *inst);
                    size_t defReg = defCollector.getResult();
                    // 如果定义了寄存器且该寄存器未被使用
                    if (defReg != 0 && useCounts[defReg] == 0)
                    {
                        // 发现死指令
                        delete inst;
                        it      = block->insts.erase(it);
                        changed = true;
                        continue;
                    }
                }
                ++it;
            }
        }
        return changed;
    }

    bool DCEPass::isSideEffect(Instruction* inst)
    {
        // 具有副作用的指令不能被移除
        switch (inst->opcode)
        {
            case Operator::STORE:
            case Operator::CALL:
            case Operator::RET:
            case Operator::BR_COND:
            case Operator::BR_UNCOND:
            case Operator::FUNCDECL:
            case Operator::FUNCDEF:
            case Operator::GLOBAL_VAR: return true;
            default: return false;
        }
    }

    size_t DCEPass::getDefReg(Instruction* inst)
    {
        DefCollector defCollector;
        apply(defCollector, *inst);
        return defCollector.getResult();
    }

    void DCEPass::collectUses(Instruction* inst, std::map<size_t, int>& useCounts)
    {
        UseCollector useCollector(useCounts);
        apply(useCollector, *inst);
    }
}  // namespace ME
