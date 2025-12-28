#ifndef __MIDDLEEND_PASS_INLINE_H__
#define __MIDDLEEND_PASS_INLINE_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_module.h>
#include <middleend/pass/inline_strategy.h>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ME
{
    // 函数内联优化
    class InlinePass : public ModulePass
    {
      public:
        InlinePass()  = default;
        ~InlinePass() = default;

        void runOnModule(Module& module) override;
        void runOnFunction(Function& function) override;

      private:
        std::map<size_t, Operand*> buildOperandMap(Function& caller, Function& callee, CallInst* callInst);
        bool inlineCall(Function& caller, Block* callBlock, CallInst* callInst, Function& callee);

        // 辅助函数：更新后继块的 phi incoming（块切分后使用）
        void updatePhiSucc(Function& caller, Operand* target, size_t oldBlockId, size_t newBlockId);
        // 辅助函数：重映射指令中的 label 目标
        void remapLabels(Instruction* inst);
        // 辅助函数：重映射单个 label 操作数
        void remapLabel(Operand*& target);

        Module* module_ = nullptr;
        std::map<size_t, size_t> labelMap_;    // 内联时的 label 映射
        std::map<size_t, Operand*> operandMap_;  // callee id -> caller operand
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_INLINE_H__
