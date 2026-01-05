#ifndef __BACKEND_RV64_PASSES_LOWERING_PHI_ELIMINATION_H__
#define __BACKEND_RV64_PASSES_LOWERING_PHI_ELIMINATION_H__

#include <backend/mir/m_module.h>
#include <backend/mir/m_function.h>
#include <backend/mir/m_block.h>
#include <backend/mir/m_instruction.h>
#include <backend/common/cfg.h>
#include <backend/common/cfg_builder.h>
#include <backend/target/target_instr_adapter.h>
#include <backend/targets/riscv64/rv64_defs.h>
#include <map>
#include <queue>
#include <vector>

namespace BE::RV64::Passes::Lowering
{
    class PhiEliminationPass
    {
      public:
        PhiEliminationPass()  = default;
        ~PhiEliminationPass() = default;

        void runOnModule(BE::Module& module, const BE::Targeting::TargetInstrAdapter* adapter);

      private:
      // 一个前驱块可能对应多个 PHI 的拷贝（多个 PHI 指令共享同一前驱）
        using CopyList = std::vector<std::pair<Register, Operand*>>;

        void runOnFunction(BE::Function* func, const BE::Targeting::TargetInstrAdapter* adapter);
        std::vector<PhiInst*> collectPhis(BE::Block* block);
        std::map<uint32_t, CopyList> aggregateCopies(const std::vector<PhiInst*>& phis);
        BE::Block* splitCriticalEdge(BE::Function* func, BE::Block* predBlock, uint32_t blockId,
                                     const BE::Targeting::TargetInstrAdapter* adapter);
        size_t findInsertIndex(BE::Block* predBlock, uint32_t blockId,
                               const BE::Targeting::TargetInstrAdapter* adapter);
        bool isDestUsedAsSrc(const Register& dst, const CopyList& copies);
        bool removeSelfCopies(CopyList& copies);
        std::vector<MInstruction*> resolveParallelCopies(CopyList copies);
    };
}  // namespace BE::RV64::Passes::Lowering

#endif  // __BACKEND_RV64_PASSES_LOWERING_PHI_ELIMINATION_H__
