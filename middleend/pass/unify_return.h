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
    // 统一返回值传递
    class UnifyReturnPass : public ModulePass
    {
      public:
        UnifyReturnPass()  = default;
        ~UnifyReturnPass() = default;

        void runOnModule(Module& module) override;        // 运行在模块级别
        void runOnFunction(Function& function) override;  // 运行在函数级别

      private:
        void unifyFunctionReturns(Function& function);  // 统一函数的返回指令

        std::vector<RetInst*> findReturnInstructions(Analysis::CFG* cfg);  // 查找函数中的所有返回指令
        Block* getBlockContaining(Function& function, Instruction* inst);  // 获取函数中包含指定指令的基本块
    };

}  // namespace ME

#endif  // __MIDDLEEND_PASS_UNIFY_RETURN_H__
