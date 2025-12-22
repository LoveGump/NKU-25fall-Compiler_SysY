#ifndef __MIDDLEEND_PASS_UNIFY_RETURN_H__
#define __MIDDLEEND_PASS_UNIFY_RETURN_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_module.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/pass/analysis/cfg.h>
#include <vector>

namespace ME
{
   // 统一返回值传递 (Unify Return Pass)
    class UnifyReturnPass : public ModulePass
    {
      public:
        UnifyReturnPass()  = default;
        ~UnifyReturnPass() = default;

        void runOnModule(Module& module) override;
        void runOnFunction(Function& function) override;

      private:
        void unifyFunctionReturns(Function& function);

        std::vector<RetInst*> findReturnInstructions(Analysis::CFG* cfg);
        Block*                getBlockContaining(Function& function, Instruction* inst);
    };

}  // namespace ME

#endif  // __MIDDLEEND_PASS_UNIFY_RETURN_H__
