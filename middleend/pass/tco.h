#ifndef __MIDDLEEND_PASS_TCO_H__
#define __MIDDLEEND_PASS_TCO_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_module.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <unordered_map>
#include <unordered_set>

namespace ME
{
    // 尾递归优化（TCO）：将自调用尾递归改写为循环
    class TCOPass : public ModulePass
    {
      public:
        TCOPass()  = default;
        ~TCOPass() = default;

        void runOnModule(Module& module) override;
        void runOnFunction(Function& function) override;

      private:
        void eliminateTailRecursion(Function& function);
        bool isVoidReturnChain(Function& function, Block* start) const;
        bool isSameParamArg(const Function& function, size_t idx, Operand* arg) const;
        bool isAllocaDerivedReg(size_t regNum, const std::unordered_map<size_t, Instruction*>& regDefs,
            std::unordered_map<size_t, bool>& memo, std::unordered_set<size_t>& visiting) const;
        bool hasAllocaDerivedArg(CallInst* call, const std::unordered_map<size_t, Instruction*>& regDefs,
            std::unordered_map<size_t, bool>& memo, std::unordered_set<size_t>& visiting) const;
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_TCO_H__
