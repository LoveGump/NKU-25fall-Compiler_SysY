#ifndef __MIDDLEEND_PASS_MEM2REG_H__
#define __MIDDLEEND_PASS_MEM2REG_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>
#include <map>
#include <vector>

namespace ME
{
    // 基本块内的mem2reg优化
    class Mem2RegPass : public FunctionPass
    {
      public:
        Mem2RegPass()  = default;
        ~Mem2RegPass() = default;

        void runOnFunction(Function& function) override;

      private:
        bool promoteMemoryToRegister(Function& function);
    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_MEM2REG_H__
