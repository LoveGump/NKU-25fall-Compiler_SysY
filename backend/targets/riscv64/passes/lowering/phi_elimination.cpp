#include <backend/targets/riscv64/passes/lowering/phi_elimination.h>
#include <debug.h>
#include <algorithm>
#include <set>

namespace BE::RV64::Passes::Lowering
{
    // 可以修改为线性时间复杂度的指令的构造
    using namespace BE;
    using namespace BE::RV64;

    // 一个前驱块可能对应多个 PHI 的拷贝（多个 PHI 指令共享同一前驱）
    using CopyList = std::vector<std::pair<Register, Operand*>>;

    static std::vector<PhiInst*> collectPhis(BE::Block* block)
    {
        std::vector<PhiInst*> phis;
        for (auto* inst : block->insts)
            if (inst && inst->kind == InstKind::PHI)
                phis.push_back(static_cast<PhiInst*>(inst));
        return phis;
    }

    // 将 PHI 指令按前驱块分组：predLabel -> [(dst, src), ...]
    static std::map<uint32_t, CopyList> aggregateCopies(const std::vector<PhiInst*>& phis)
    {
        std::map<uint32_t, CopyList> copiesPerPred;
        for (auto* phi : phis)
            for (auto& [predLabel, srcOp] : phi->incomingVals)
                copiesPerPred[predLabel].push_back({phi->resReg, srcOp});
        return copiesPerPred;
    }

    /**
     * 关键边分裂：当条件跳转指向当前块时，需要插入中间块避免污染另一分支
     *
     * 例如：
     *   BB_pred:
     *     beq x1, x2, BB_target   <- 条件跳转到目标块
     *     j BB_other              <- 无条件跳转到另一块
     *
     * 如果直接在 BB_pred 插入拷贝，会影响 BB_other 分支。
     * 解决方案：插入中间块 BB_edge，让条件跳转指向它，再由它跳转到原目标。
     */
    static BE::Block* splitCriticalEdge(BE::Function* func, BE::Block* predBlock, uint32_t blockId,
                                        const BE::Targeting::TargetInstrAdapter* adapter)
    {
        
        // 查找指向目标块的条件跳转
        auto& insts = predBlock->insts;
        for (size_t i = 0; i < insts.size(); ++i)
        {
            auto* ri = dynamic_cast<RV64::Instr*>(insts[i]);
            if (!ri || !adapter->isCondBranch(ri)) continue; // 非（条件跳转）语句直接跳过
            if (adapter->extractBranchTarget(ri) != static_cast<int>(blockId)) continue; // 目标块不匹配跳过

            // 需要分裂：cond+uncond 或 cond 作为末尾（另一边 fallthrough）
            bool needSplit = (i + 1 < insts.size() && adapter->isUncondBranch(insts[i + 1]))
                          || (i == insts.size() - 1);
            if (!needSplit) break;

            // 创建中间块，重定向条件跳转
            uint32_t newId = func->blocks.rbegin()->first + 1;
            auto* edgeBlock = new BE::Block(newId);

            // 跳转指令目标改为中间块
            ri->label = RV64::Label(static_cast<int>(newId));
            ri->use_label = true;

            // 中间块跳转到原目标块
            edgeBlock->insts.push_back(createJInst(RV64::Operator::JAL,
                Register(0, BE::I64, false), RV64::Label(static_cast<int>(blockId))));

            // 插入新块到函数
            func->blocks[newId] = edgeBlock;
            return edgeBlock;
        }
        return predBlock;
    }

    // 确定拷贝指令的插入位置
    static size_t findInsertIndex(BE::Block* predBlock, uint32_t blockId,
                                  const BE::Targeting::TargetInstrAdapter* adapter)
    {
        size_t n = predBlock->insts.size();
        if (n == 0) return 0;

        auto* last = predBlock->insts.back();

        // 无条件跳转或返回：插入到指令之前
        // 条件跳转：经过 splitCriticalEdge 后一定是 fallthrough，插入到末尾
        if (adapter->isUncondBranch(last) || adapter->isReturn(last))
            return n - 1;

        return n;
    }

    // 检查 dst 是否被用作其他拷贝的 src
    static bool isDestUsedAsSrc(const Register& dst, const CopyList& copies)
    {
        for (auto& [_, srcOp] : copies)
            if (auto* srcReg = dynamic_cast<RegOperand*>(srcOp))
                if (srcReg->reg == dst) return true;
        return false;
    }

    // 移除 dst == src 的自拷贝
    static bool removeSelfCopies(CopyList& copies)
    {
        size_t before = copies.size();
        for (auto it = copies.begin(); it != copies.end(); )
        {
            auto* srcReg = dynamic_cast<RegOperand*>(it->second);
            if (srcReg && srcReg->reg == it->first)
                it = copies.erase(it);
            else
                ++it;
        }
        return copies.size() < before;
    }

    /**
     * 并行拷贝消解算法
     *
     * PHI 的语义是"并行赋值"，但实际执行是顺序的，需要处理依赖：
     *   1. 无依赖：直接执行 (dst 不被其他拷贝用作 src)
     *   2. 有环：使用临时寄存器打破环 (a <- b, b <- a => tmp <- a, a <- b, b <- tmp)
     */
    static std::vector<MInstruction*> resolveParallelCopies(CopyList copies)
    {
        std::vector<MInstruction*> result;

        while (!copies.empty())
        {
            if (removeSelfCopies(copies)) continue;

            // 优先处理无依赖的拷贝
            bool found = false;
            for (auto it = copies.begin(); it != copies.end(); ++it)
            {
                // 遍历指令，如果没有被用作其他拷贝的源，则可以安全执行
                if (!isDestUsedAsSrc(it->first, copies))
                {
                    result.push_back(createMove(new RegOperand(it->first), it->second, "phi-elim"));
                    copies.erase(it);
                    found = true;
                    break;
                }
            }
            if (found) continue;

            // 剩余拷贝一定存在环，用临时寄存器打破
            std::map<Register, Register> destToSrc;
            for (auto& [dst, srcOp] : copies)
                if (auto* srcReg = dynamic_cast<RegOperand*>(srcOp))
                    destToSrc[dst] = srcReg->reg;

            // 从任意节点开始找环
            Register start = destToSrc.begin()->first;
            Register cur = start;
            std::vector<Register> cycle;
            do {
                cycle.push_back(cur);
                cur = destToSrc[cur];
            } while (!(cur == start));

            // 环处理：tmp <- first; first <- second; ... ; last <- tmp
            Register tmp = getVReg(cycle.front().dt);
            result.push_back(createMove(new RegOperand(tmp), new RegOperand(cycle.front()), "phi-cycle"));

            for (size_t i = 0; i + 1 < cycle.size(); ++i)
                result.push_back(createMove(new RegOperand(cycle[i]), new RegOperand(destToSrc[cycle[i]]), "phi-cycle"));

            result.push_back(createMove(new RegOperand(cycle.back()), new RegOperand(tmp), "phi-cycle"));

            std::set<Register> cycleSet(cycle.begin(), cycle.end());
            for (auto it = copies.begin(); it != copies.end(); )
            {
                if (cycleSet.count(it->first))
                    it = copies.erase(it);
                else
                    ++it;
            }
        }

        return result;
    }

    void PhiEliminationPass::runOnModule(BE::Module& module, const BE::Targeting::TargetInstrAdapter* adapter)
    {
        for (auto* func : module.functions)
            runOnFunction(func, adapter);
    }

    void PhiEliminationPass::runOnFunction(BE::Function* func, const BE::Targeting::TargetInstrAdapter* adapter)
    {
        if (!func || func->blocks.empty()) return;

        for (auto& [blockId, block] : func->blocks)
        {
            if (!block) continue;

            // 收集 PHI 指令
            auto phis = collectPhis(block);
            if (phis.empty()) continue;

            // 按前驱块聚合拷贝
            auto copiesPerPred = aggregateCopies(phis);

            for (auto& [predLabel, copies] : copiesPerPred)
            {
                auto predIt = func->blocks.find(predLabel);
                if (predIt == func->blocks.end() || !predIt->second) continue;

                // 要插入的块号
                BE::Block* predBlock = splitCriticalEdge(func, predIt->second, blockId, adapter);

                // 要插入的位置
                size_t insertIdx = findInsertIndex(predBlock, blockId, adapter);

                // 生成拷贝指令
                auto newInsts = resolveParallelCopies(std::move(copies));

                predBlock->insts.insert(predBlock->insts.begin() + insertIdx, newInsts.begin(), newInsts.end());
            }

            // 移除 PHI 指令
            std::deque<MInstruction*> filtered;
            for (auto* inst : block->insts)
                if (!inst || inst->kind != InstKind::PHI)
                    filtered.push_back(inst);
            block->insts = std::move(filtered);
        }
    }
}  // namespace BE::RV64::Passes::Lowering
