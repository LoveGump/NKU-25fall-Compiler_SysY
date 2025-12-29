#include <middleend/pass/dce.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_operand.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <algorithm>

namespace ME
{
    void DCEPass::runOnFunction(Function& function)
    {
        // DCE（Dead Code Elimination）：
        // 以“结果寄存器无人使用”为判据，删除无副作用的死指令。
        // 由于删除会让一些寄存器的 use 计数归零，需要迭代到收敛。

        // 反复执行直到不再有改动
        bool changed = true;
        while (changed) { changed = eliminateDeadCode(function); }
    }

    bool DCEPass::eliminateDeadCode(Function& function)
    {
        std::map<size_t, int> useCounts;        // 寄存器使用次数   寄存器编号->使用次数
        bool                  changed = false;  // 是否有改动

        // 1) 统计全函数的寄存器 use 次数
        UseCollector useCollector(useCounts);
        // 遍历所有指令，收集使用信息
        for (auto& [id, block] : function.blocks)
        {
            for (auto inst : block->insts) { apply(useCollector, *inst); }
        }

        // 2) 在线性遍历中删除死指令：
        //    - 只删除“无副作用”指令
        //    - 且该指令定义的寄存器 defReg 的 use 次数为 0
        DefCollector defCollector;  // 用于获取指令定义的寄存器

        for (auto& [id, block] : function.blocks)
        {
            std::deque<Instruction*> newInsts;  // 新的指令列表

            for (auto inst : block->insts)
            {
                bool isDead = false;
                if (!isSideEffect(inst))
                {
                    // 对于没有副作用的指令，检查其定义的寄存器是否被使用
                    apply(defCollector, *inst);
                    size_t defReg = defCollector.getResult();
                    // 如果定义了寄存器且该寄存器未被使用，则为死指令
                    if (defReg != 0 && useCounts[defReg] == 0)
                    {
                        isDead  = true;
                        changed = true;
                        delete inst;  // 删除死指令
                    }
                }

                if (!isDead)
                {
                    newInsts.push_back(inst);  // 保留非死指令
                }
            }

            // 一次性替换整个指令列表（O(1) swap）
            block->insts = newInsts;
        }

        return changed;
    }

    bool DCEPass::isSideEffect(Instruction* inst)
    {
        // 具有副作用的指令不能被移除
        switch (inst->opcode)
        {
            case Operator::STORE:  // 改变内存状态
            case Operator::CALL:   // 可能改变内存或有其他副作用
            case Operator::RET:    // 会改变控制流
            case Operator::BR_COND:
            case Operator::BR_UNCOND: return true;
            default: return false;
        }
    }
}  // namespace ME

/*
DCE 流程总结（对应本文件实现）：
1) 扫描全函数，统计每个寄存器的 use 次数。
2) 遍历指令：若指令无副作用且其 def 寄存器 use 次数为 0，则删除该指令。
3) 因为删除会减少其它寄存器的 use 次数，重复执行 1)-2) 直到不再发生删除。
*/
