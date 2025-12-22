#ifndef __MIDDLEEND_PASS_DCE_H__
#define __MIDDLEEND_PASS_DCE_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_instruction.h>
#include <set>
#include <map>

namespace ME
{
    class DCEPass : public FunctionPass
    {
      public:
        DCEPass()  = default;
        ~DCEPass() = default;

        void runOnFunction(Function& function) override;

      private:
        bool   eliminateDeadCode(Function& function);
        bool   isSideEffect(Instruction* inst);
        void   collectUses(Instruction* inst, std::map<size_t, int>& useCounts);
        size_t getDefReg(Instruction* inst);
    };
}  // namespace ME

#endif
