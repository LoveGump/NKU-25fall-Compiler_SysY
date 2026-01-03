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

                // 如果前驱块以「条件跳转 + 无条件跳转」结束，并且当前块是条件跳转的目标，
                // 直接在原块中插入拷贝会污染另一条分支的寄存器，需拆分边。
                auto splitEdgeForCondTarget = [&](BE::Block*& pb) {
                    auto& insts = pb->insts;
                    if (insts.empty()) return;

                    auto createEdgeBlock = [&](RV64::Instr* condInst) {
                        uint32_t newId = func->blocks.empty() ? 0 : (func->blocks.rbegin()->first + 1);
                        auto*    edgeBlock = new BE::Block(newId);

                        // 重定向条件跳转的目标到新块
                        condInst->label     = RV64::Label(static_cast<int>(newId));
                        condInst->use_label = true;

                        // 新块只需一个无条件跳转回原目标
                        edgeBlock->insts.push_back(createJInst(
                            RV64::Operator::JAL, Register(0, BE::I64, false), RV64::Label(static_cast<int>(blockId))));
                        func->blocks[newId] = edgeBlock;
                        pb                  = edgeBlock;
                    };

                    for (size_t i = 0; i < insts.size(); ++i)
                    {
                        auto* condInst   = insts[i];
                        if (!adapter->isCondBranch(condInst)) continue;

                        int trueTarget  = adapter->extractBranchTarget(condInst);
                        if (trueTarget != static_cast<int>(blockId)) continue;

                        bool hasUncondFalse = (i + 1 < insts.size()) && adapter->isUncondBranch(insts[i + 1]);
                        bool isOnlyCond     = (i == insts.size() - 1);

                        // 需要拆分的两种情况：
                        // 1) cond + uncond，且当前块是 cond 的目标
                        // 2) cond 作为末尾跳转（另一条边为 fallthrough），当前块是 cond 的目标
                        if (hasUncondFalse || isOnlyCond)
                        {
                            if (auto* ri = dynamic_cast<RV64::Instr*>(condInst)) { createEdgeBlock(ri); }
                            return;
                        }
                    }
                };
                splitEdgeForCondTarget(predBlock);

                auto computeInsertIdx = [&](BE::Block* pb) -> size_t {
                    size_t n = pb->insts.size();
                    if (n == 0) return 0;

                    auto* last = pb->insts.back();
                    // 如果以单条条件跳转结束且目标不是当前块，则当前块必然是fallthrough，
                    // 应该把拷贝放在分支之后（只在落空路径执行）。
                    if (adapter->isCondBranch(last))
                    {
                        int t = adapter->extractBranchTarget(last);
                        if (t != static_cast<int>(blockId)) return n;  // fallthrough edge
                    }

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
                    if (adapter->isCondBranch(last) || adapter->isUncondBranch(last) || adapter->isReturn(last))
                        return n - 1;
                    return n;
                };
                size_t insertIdx = computeInsertIdx(predBlock);

                std::vector<MInstruction*> newInsts;

                // 并行拷贝消解
                while (!copies.empty())
                {
                    // 先移除自拷贝
                    bool removedSelf = false;
                    for (auto it = copies.begin(); it != copies.end();)
                    {
                        auto* srcReg = dynamic_cast<RegOperand*>(it->second);
                        if (srcReg && srcReg->reg == it->first)
                        {
                            it           = copies.erase(it);
                            removedSelf  = true;
                        }
                        else
                            ++it;
                    }
                    if (removedSelf) continue;

                    // 选择一个“目标不再被其他拷贝作为源”的拷贝，安全地顺序执行
                    auto destUsedAsSrc = [&](const Register& dst) {
                        for (auto& cp : copies)
                        {
                            auto* srcReg = dynamic_cast<RegOperand*>(cp.second);
                            if (srcReg && srcReg->reg == dst) return true;
                        }
                        return false;
                    };

                    bool progress = false;
                    for (auto it = copies.begin(); it != copies.end(); ++it)
                    {
                        if (!destUsedAsSrc(it->first))
                        {
                            newInsts.push_back(createMove(new RegOperand(it->first), it->second, "phi-elim"));
                            copies.erase(it);
                            progress = true;
                            break;
                        }
                    }
                    if (progress) continue;

                    // 剩余拷贝必然形成环，按环进行旋转拷贝
                    std::map<Register, Register> destToSrc;
                    for (auto& cp : copies)
                    {
                        if (auto* srcReg = dynamic_cast<RegOperand*>(cp.second)) { destToSrc[cp.first] = srcReg->reg; }
                    }

                    Register              start = copies.front().first;
                    std::vector<Register> cycle;
                    Register              cur = start;
                    do
                    {
                        cycle.push_back(cur);
                        auto it = destToSrc.find(cur);
                        if (it == destToSrc.end())
                        {
                            break;
                        }
                        cur = it->second;
                    } while (!(cur == start));

                    // 如果未形成闭环，降级处理为普通拷贝，避免死循环
                    if (cycle.size() < 2 || !(cur == start))
                    {
                        auto cp = copies.front();
                        newInsts.push_back(createMove(new RegOperand(cp.first), cp.second, "phi-elim"));
                        copies.erase(copies.begin());
                        continue;
                    }

                    Register tmp = getVReg(cycle.front().dt);
                    newInsts.push_back(createMove(new RegOperand(tmp), new RegOperand(cycle.front()), "phi-cycle"));

                    for (size_t i = 0; i + 1 < cycle.size(); ++i)
                    {
                        Register dst = cycle[i];
                        Register src = destToSrc[dst];
                        newInsts.push_back(createMove(new RegOperand(dst), new RegOperand(src), "phi-cycle"));
                    }

                    Register lastDst = cycle.back();
                    newInsts.push_back(createMove(new RegOperand(lastDst), new RegOperand(tmp), "phi-cycle"));

                    auto inCycle = [&](const std::pair<Register, Operand*>& cp) {
                        return std::find(cycle.begin(), cycle.end(), cp.first) != cycle.end();
                    };
                    copies.erase(std::remove_if(copies.begin(), copies.end(), inCycle), copies.end());
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
