#ifndef __MIDDLEEND_PASS_SIMPLIFY_CFG_H__
#define __MIDDLEEND_PASS_SIMPLIFY_CFG_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>

namespace ME
{
    // 控制流简化：删除只有一条无条件跳转的块
    class SimplifyCFGPass : public FunctionPass
    {
      public:
        SimplifyCFGPass()  = default;
        ~SimplifyCFGPass() = default;

        void runOnFunction(Function& function) override;
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_SIMPLIFY_CFG_H__
