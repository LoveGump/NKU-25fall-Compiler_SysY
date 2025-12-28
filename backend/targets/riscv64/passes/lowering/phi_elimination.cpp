#include <backend/targets/riscv64/passes/lowering/phi_elimination.h>
#include <debug.h>
#include <algorithm>
#include <set>

namespace BE::RV64::Passes::Lowering
{
    using namespace BE;
    using namespace BE::RV64;

    void PhiEliminationPass::runOnModule(BE::Module& module, const BE::Targeting::TargetInstrAdapter* adapter)
    {
        if (module.functions.empty()) return;
        for (auto* func : module.functions) runOnFunction(func, adapter);
    }

    void PhiEliminationPass::runOnFunction(BE::Function* func, const BE::Targeting::TargetInstrAdapter* adapter)
    {
        if (!func || func->blocks.empty()) return;

        // 构建 CFG 以获取前驱信息
        BE::MIR::CFGBuilder cfgBuilder(adapter);
        auto* cfg = cfgBuilder.buildCFGForFunction(func);
        if (!cfg) return;

        // 遍历所有基本块，收集并消解 PHI
        for (auto& [blockId, block] : func->blocks)
        {
            if (!block) continue;

            // 收集当前块的所有 PHI 指令
            std::vector<PhiInst*> phis;
            for (auto* inst : block->insts)
            {
                if (inst && inst->kind == InstKind::PHI)
                    phis.push_back(static_cast<PhiInst*>(inst));
            }

            if (phis.empty()) continue;

            // 按前驱块聚合需要插入的拷贝
            std::map<uint32_t, std::vector<std::pair<Register, Operand*>>> copiesPerPred;
            for (auto* phi : phis)
            {
                Register dst = phi->resReg;

                // 对每个 incoming value，在对应的前驱块末尾插入 MOVE
                for (auto& [predLabel, srcOp] : phi->incomingVals)
                {
                    copiesPerPred[predLabel].push_back({dst, srcOp});
                }
            }

            // 将聚合后的拷贝插入对应的前驱块，使用并行拷贝算法处理互相依赖
            for (auto& [predLabel, copies] : copiesPerPred)
            {
                auto predIt = func->blocks.find(predLabel);
                if (predIt == func->blocks.end() || !predIt->second) continue;
                BE::Block* predBlock = predIt->second;

                auto computeInsertIdx = [&](BE::Block* pb) -> size_t {
                    size_t n = pb->insts.size();
                    // 优先放在指向当前块的终结指令之前
                    for (size_t i = n; i > 0; --i)
                    {
                        auto* inst = pb->insts[i - 1];
                        if (!inst || inst->kind != InstKind::TARGET) continue;
                        if (adapter->isCondBranch(inst) || adapter->isUncondBranch(inst))
                        {
                            int t = adapter->extractBranchTarget(inst);
                            if (t == static_cast<int>(blockId)) return i - 1;
                        }
                    }
                    // 否则放在最后一条终结指令前（若存在）
                    if (n > 0)
                    {
                        auto* last = pb->insts.back();
                        if (adapter->isCondBranch(last) || adapter->isUncondBranch(last) || adapter->isReturn(last))
                            return n - 1;
                    }
                    return n;
                };
                size_t insertIdx = computeInsertIdx(predBlock);

                std::vector<MInstruction*> newInsts;

                auto hasDest = [&](Register r) {
                    for (auto& cp : copies)
                        if (cp.first == r) return true;
                    return false;
                };

                // 并行拷贝消解
                while (!copies.empty())
                {
                    // 跳过自拷贝
                    bool removedSelf = false;
                    for (auto it = copies.begin(); it != copies.end(); ++it)
                    {
                        auto* srcReg = dynamic_cast<RegOperand*>(it->second);
                        if (srcReg && srcReg->reg == it->first)
                        {
                            copies.erase(it);
                            removedSelf = true;
                            break;
                        }
                    }
                    if (removedSelf) continue;

                    // 尝试选择一个源不依赖其他目标的拷贝
                    bool progress = false;
                    for (auto it = copies.begin(); it != copies.end(); ++it)
                    {
                        auto* srcReg = dynamic_cast<RegOperand*>(it->second);
                        if (!srcReg || !hasDest(srcReg->reg))
                        {
                            newInsts.push_back(createMove(new RegOperand(it->first), it->second, "phi-elim"));
                            copies.erase(it);
                            progress = true;
                            break;
                        }
                    }
                    if (progress) continue;

                    // 剩余拷贝形成环：使用临时寄存器打破
                    auto& cyc      = copies.front();
                    auto* srcRegOp = dynamic_cast<RegOperand*>(cyc.second);
                    if (!srcRegOp)
                    {
                        newInsts.push_back(createMove(new RegOperand(cyc.first), cyc.second, "phi-elim"));
                        copies.erase(copies.begin());
                        continue;
                    }

                    Register tmp = getVReg(srcRegOp->reg.dt);
                    newInsts.push_back(createMove(new RegOperand(tmp), cyc.second, "phi-spill"));

                    // 将该源替换为临时寄存器，解除环
                    for (auto& cp : copies)
                    {
                        if (auto* s = dynamic_cast<RegOperand*>(cp.second); s && s->reg == srcRegOp->reg)
                            cp.second = new RegOperand(tmp);
                    }
                }

                predBlock->insts.insert(predBlock->insts.begin() + insertIdx, newInsts.begin(), newInsts.end());
            }

            // 从当前块中删除所有 PHI 指令
            auto newEnd = std::remove_if(block->insts.begin(), block->insts.end(),
                [](MInstruction* inst) { return inst && inst->kind == InstKind::PHI; });
            block->insts.erase(newEnd, block->insts.end());
        }

        delete cfg;
    }
}  // namespace BE::RV64::Passes::Lowering
