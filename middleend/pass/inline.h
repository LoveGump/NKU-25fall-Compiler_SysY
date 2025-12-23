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
        // 构建调用图判断递归
        bool         isRecursive(Function& target, Module& module);
        // 查找被调用函数
        Function*    findCallee(Module& module, const std::string& name);
        // 将被内联函数的寄存器映射到调用者寄存器
        Operand*     mapOperand(Operand* op, const std::map<size_t, Operand*>& operandMap);
        // 深拷贝 IR 指令
        Instruction* cloneInstruction(const Instruction* inst);
        // 重映射跳转标签
        void         remapLabels(Instruction* inst, const std::map<size_t, size_t>& labelMap);
        // 更新 phi 的前驱标签
        void         replacePhiIncoming(Block* block, size_t oldLabel, size_t newLabel);
        // 构造寄存器映射表
        std::map<size_t, Operand*> buildOperandMap(Function& caller, Function& callee, CallInst* callInst);
        // 定位函数入口块
        Block*      findEntryBlock(Function& func);
        // 内联单条 call 指令
        bool        inlineCall(Function& caller, Block* callBlock, size_t callIndex, CallInst* callInst, Function& callee);
        // 结合策略执行内联
        bool        inlineWithStrategy(Function& function, InlineStrategy& strategy);
        // 在函数内定位 call 指令的位置
        bool        findCallLocation(Function& function, CallInst* callInst, Block*& block, size_t& index);

        Module* module_ = nullptr;
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_INLINE_H__
