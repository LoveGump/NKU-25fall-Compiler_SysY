#ifndef __MIDDLEEND_PASS_ADCE_H__
#define __MIDDLEEND_PASS_ADCE_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_instruction.h>
#include <set>
#include <map>
#include <vector>

namespace ME
{
    // 激进死代码消除
    class ADCEPass : public FunctionPass
    {
      public:
        ADCEPass()  = default;
        ~ADCEPass() = default;

        void runOnFunction(Function& function) override;

      private:
        // 标记活跃指令
        void markLive(Function& function);
        // 移除死代码
        bool removeDeadCode(Function& function);
        // 判断指令是否有副作用，和dcepass中一样
        bool isSideEffect(Instruction* inst);
        // 清理不可达块造成的 Phi 节点残留
        void cleanUp(Function& function);

        std::set<Instruction*> liveInsts;   // 活跃指令集合
        std::vector<int>       postImmDom;  // 后支配树的直接支配者
        size_t                 numBlocks;   // 基本块数量
    };
}  // namespace ME

#endif
