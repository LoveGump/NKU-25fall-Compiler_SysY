#include <middleend/pass/dce.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <algorithm>

namespace ME
{
    void DCEPass::runOnFunction(Function& function)
    {
        // 反复执行直到不再有改动
        bool changed = true;
        while (changed) { changed = eliminateDeadCode(function); }
    }

    bool DCEPass::eliminateDeadCode(Function& function)
    {
        std::map<size_t, int> useCounts;    // 寄存器使用次数   寄存器编号->使用次数
        bool                  changed = false; // 是否有改动

        // 1. 收集使用次数
        UseCollector useCollector(useCounts); 
        // 遍历所有指令，收集使用信息
        for (auto& [id, block] : function.blocks)
        {
            for (auto inst : block->insts) { apply(useCollector, *inst); }
        }

        // 2. 识别并移除死代码
        DefCollector defCollector; // 用于获取指令定义的寄存器
        // 遍历所有指令，识别死指令并移除
        for (auto& [id, block] : function.blocks)
        {
            // 遍历基本块中的指令
            auto it = block->insts.begin();
            while (it != block->insts.end())
            {
                Instruction* inst = *it;
                if (!isSideEffect(inst))
                {
                    // 对于没有副作用的指令，检查其定义的寄存器是否被使用
                    apply(defCollector, *inst);
                    size_t defReg = defCollector.getResult();
                    // 如果定义了寄存器且该寄存器未被使用，则为死指令
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
            case Operator::STORE:   // 改变内存状态
            case Operator::CALL:    // 可能改变内存或有其他副作用
            case Operator::RET:     // 会改变控制流
            case Operator::BR_COND: 
            case Operator::BR_UNCOND:
            case Operator::FUNCDECL:    // 函数声明有副作用
            case Operator::FUNCDEF:     // 函数定义有副作用
            case Operator::GLOBAL_VAR:  // 全局变量声明有副作用
                return true;
            default: return false;
        }
    }
}  // namespace ME
