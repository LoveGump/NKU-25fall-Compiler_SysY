#ifndef __MIDDLEEND_PASS_CSE_H__
#define __MIDDLEEND_PASS_CSE_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>

namespace ME
{
    class Instruction;

    // 标量公共子表达式消除
    // 将指令的清理工作留给 DCE Pass
    class CSEPass : public FunctionPass
    {
      public:
        CSEPass()  = default;
        ~CSEPass() = default;

        void runOnFunction(Function& function) override;

      private:
        // 块内的cse
        bool runBlockLocalCSE(Function& function);
        bool runDominatorCSE(Function& function);
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_CSE_H__
