#include <middleend/pass/unify_return.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/module/ir_operand.h>
#include <algorithm>
#include <iostream>

namespace ME
{
    void UnifyReturnPass::runOnModule(Module& module)
    {
        // 对模块中的每个函数运行统一返回值传递的处理
        for (auto* function : module.functions) unifyFunctionReturns(*function);
    }

    void UnifyReturnPass::runOnFunction(Function& function) { unifyFunctionReturns(function); }

    void UnifyReturnPass::unifyFunctionReturns(Function& function)
    {
        // 获取函数的控制流图 (CFG)
        auto* cfg = Analysis::AM.get<Analysis::CFG>(function);

        auto retInstructions = findReturnInstructions(cfg);

        if (retInstructions.size() <= 1) return;  // 如果函数只有一个或没有返回指令，则无需处理

        // 创建一个新的退出基本块
        Block* exitBlock = function.createBlock();

        // 收集所有返回指令的返回值及其所在基本块标签pair<返回值,标签>
        std::vector<std::pair<Operand*, Operand*>> returnValues;
        DataType                                   returnType = DataType::VOID; // 函数的返回类型

        for (auto* retInst : retInstructions)
        {
            Block* containingBlock = getBlockContaining(function, retInst);
            if (!containingBlock) continue;

            returnType = retInst->rt;

            // 通过id 获取标签操作数
            Operand* labelOp = getLabelOperand(containingBlock->blockId);

            if (retInst->res)
                returnValues.push_back({retInst->res, labelOp});
            else
                returnValues.push_back({nullptr, labelOp});

            // 用无条件跳转指令替换原有的返回指令
            auto it = std::find(containingBlock->insts.begin(), containingBlock->insts.end(), retInst);
            if (it != containingBlock->insts.end())
            {
                Operand* exitLabel  = getLabelOperand(exitBlock->blockId); // 获取退出块的标签操作数
                auto*    branchInst = new BrUncondInst(exitLabel);  // 创建无条件跳转指令
                *it                 = branchInst;                   // 替换原有的返回指令
                delete retInst;  // 删除原有的返回指令   
            }
        }

        // 在退出基本块中创建一个 Phi 指令来选择正确的返回值
        if (returnType != DataType::VOID && !returnValues.empty())
        {
            // 非空的返回值及其对应的标签
            std::vector<std::pair<Operand*, Operand*>> validValues;
            for (auto& [val, label] : returnValues)
                if (val != nullptr) validValues.push_back({val, label});

            if (!validValues.empty())
            {
                // 创建一个新的寄存器用于存储返回值
                Operand* resultReg = getRegOperand(function.getNewRegId());

                auto* phiInst = new PhiInst(returnType, resultReg);
                for (auto& [val, label] : validValues) phiInst->addIncoming(val, label);
                exitBlock->insertBack(phiInst);

                auto* finalRet = new RetInst(returnType, resultReg);
                exitBlock->insertBack(finalRet);
            }
            else
            {
                // 所有返回指令均无返回值，插入 void 返回指令
                auto* finalRet = new RetInst(DataType::VOID, nullptr);
                exitBlock->insertBack(finalRet);
            }
        }
        else
        {
            // 函数无返回值，插入 void 返回指令
            auto* finalRet = new RetInst(DataType::VOID, nullptr);
            exitBlock->insertBack(finalRet);
        }

        // 由于在 `if (retInstructions.size() <= 1) return;` 处没有退出
        // 我们可以确定该 pass 的执行一定向当前函数插入了新的基本块并修改了跳转关系
        // 因此需要 invalidate 当前函数的 CFG 缓存
        Analysis::AM.invalidate(function);
    }

    std::vector<RetInst*> UnifyReturnPass::findReturnInstructions(Analysis::CFG* cfg)
    {
        // 查找函数中的所有返回指令
        std::vector<RetInst*> retInstructions;

        // 遍历 CFG 中的所有基本块，收集返回指令
        for (auto& [blockId, block] : cfg->id2block)
        {
            for (auto* inst : block->insts)
            {
                if (inst->opcode != Operator::RET) continue;
                retInstructions.push_back(static_cast<RetInst*>(inst));
            }
        }

        return retInstructions;
    }

    Block* UnifyReturnPass::getBlockContaining(Function& function, Instruction* inst)
    {
        // 获取包含指定指令的基本块
        auto* cfg = Analysis::AM.get<Analysis::CFG>(function);
        for (auto& [blockId, block] : cfg->id2block)
        {
            auto it = std::find(block->insts.begin(), block->insts.end(), inst);
            if (it != block->insts.end()) return block;
        }
        return nullptr;
    }

}  // namespace ME
