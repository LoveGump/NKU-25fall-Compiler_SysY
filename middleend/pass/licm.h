#ifndef __MIDDLEEND_PASS_LICM_H__
#define __MIDDLEEND_PASS_LICM_H__

#include <interfaces/middleend/pass.h>
#include <middleend/module/ir_module.h>
#include <middleend/module/ir_function.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/dominfo.h>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>

namespace ME
{
    // 循环不变量外提
    class LICMPass : public ModulePass
    {
      public:
        LICMPass()  = default;
        ~LICMPass() = default;

        void runOnModule(Module& module) override;
        void runOnFunction(Function& function) override;

      private:
        std::unordered_set<std::string> immutableGlobals;

      // 循环信息结构体
        struct LoopInfo
        {
            size_t           header = 0;
            std::set<size_t> blocks;
            std::set<size_t> latches;
        };

        void runOnFunctionImpl(Function& function);
        void collectImmutableGlobals(Module& module);
        bool findLoops(Function& function, Analysis::CFG* cfg, Analysis::DomInfo* dom,
            std::vector<LoopInfo>& loops) const;
        bool dominates(int dom, int node, const std::vector<int>& imm_dom) const;
        bool dominatesAllLatches(size_t blockId, const LoopInfo& loop, const std::vector<int>& imm_dom) const;
        void collectLoopNodes(size_t header, size_t latch, const std::vector<std::vector<size_t>>& invG,
            std::set<size_t>& loopNodes) const;

        Block* getOrCreatePreheader(Function& function, Analysis::CFG* cfg, LoopInfo& loop);
        void   redirectPredsToPreheader(Function& function, const std::set<size_t>& preds, size_t headerId,
            size_t preheaderId);
        void   updateHeaderPhis(Function& function, Block* header, const std::set<size_t>& predsOutside,
              size_t preheaderId);
        bool   isSingleSuccToHeader(Analysis::CFG* cfg, size_t predId, size_t headerId) const;

        void buildDefUseMaps(Function& function, std::unordered_map<size_t, Instruction*>& regDefs,
            std::unordered_map<size_t, size_t>& regDefBlock, std::unordered_map<Instruction*, size_t>& instBlock,
            std::map<size_t, std::vector<Instruction*>>& userMap);

        bool isLoopInvariantOperand(size_t reg, const LoopInfo& loop,
            const std::unordered_map<size_t, size_t>& regDefBlock, const std::set<size_t>& invariantRegs) const;
        bool areUsesInsideLoop(size_t defReg, const LoopInfo& loop,
            const std::map<size_t, std::vector<Instruction*>>& userMap,
            const std::unordered_map<Instruction*, size_t>& instBlock) const;
        bool isInvariantInst(Instruction* inst, const LoopInfo& loop,
            const std::unordered_map<size_t, size_t>& regDefBlock, const std::set<size_t>& invariantRegs,
            const std::map<size_t, std::vector<Instruction*>>& userMap,
            const std::unordered_map<Instruction*, size_t>& instBlock, const std::vector<int>& imm_dom,
            const std::set<Operand*>& loopStoreGlobals, bool loopHasCall) const;

        void collectLoopEffects(Function& function, const LoopInfo& loop, std::set<Operand*>& loopStoreGlobals,
            bool& loopHasCall) const;

        void collectInvariantInsts(Function& function, const LoopInfo& loop,
            const std::unordered_map<size_t, size_t>& regDefBlock,
            const std::map<size_t, std::vector<Instruction*>>& userMap,
            const std::unordered_map<Instruction*, size_t>& instBlock, const std::vector<int>& imm_dom,
            bool restrictHeader, const std::set<Operand*>& loopStoreGlobals, bool loopHasCall,
            std::set<Instruction*>& invariantInsts, std::set<size_t>& invariantRegs);

        void buildHoistOrder(Function& function, const LoopInfo& loop, const std::set<Instruction*>& invariantInsts,
            std::vector<Instruction*>& hoistOrder);
        void removeInstFromBlock(Block* block, Instruction* inst);
        void hoistInstructions(Block* preheader, const std::vector<Instruction*>& hoistOrder,
            std::unordered_map<Instruction*, size_t>& instBlock, std::unordered_map<size_t, size_t>& regDefBlock,
            Function& function, const LoopInfo& loop, const std::vector<int>& imm_dom);

    };
}  // namespace ME

#endif  // __MIDDLEEND_PASS_LICM_H__
