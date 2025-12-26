#ifndef __MIDDLEEND_PASS_DCE_H__
#define __MIDDLEEND_PASS_DCE_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_instruction.h>
#include <set>
#include <map>

namespace ME
{
    // 死代码消除（删除没有use的def）
    class DCEPass : public FunctionPass
    {
      public:
        DCEPass()  = default;
        ~DCEPass() = default;

        // 运行DCE优化
        void runOnFunction(Function& function) override;

      private:
        // 执行死代码消除，返回是否有改动
        bool eliminateDeadCode(Function& function);

        // 判断指令是否有副作用
        bool isSideEffect(Instruction* inst);
    };
}  // namespace ME

#endif
