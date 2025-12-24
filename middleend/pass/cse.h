#ifndef __MIDDLEEND_PASS_CSE_H__
#define __MIDDLEEND_PASS_CSE_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>

namespace ME
{
    // 标量公共子表达式消除（CSE）
    class CSEPass : public FunctionPass
    {
      public:
        CSEPass()  = default;
        ~CSEPass() = default;

        void runOnFunction(Function& function) override;
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_CSE_H__
