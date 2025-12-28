#include <backend/targets/riscv64/passes/lowering/stack_lowering.h>
#include <backend/mir/m_function.h>
#include <backend/mir/m_instruction.h>
#include <backend/mir/m_defs.h>
#include <backend/targets/riscv64/rv64_defs.h>
#include <backend/targets/riscv64/rv64_reg_info.h>
#include <algorithm>
#include <set>
#include <cstring>

namespace BE::RV64::Passes::Lowering
{
    using namespace BE;
    using namespace BE::RV64;

    void StackLoweringPass::runOnModule(BE::Module& module)
    {
        for (auto* func : module.functions) lowerFunction(func);
    }

    void StackLoweringPass::lowerFunction(BE::Function* func)
    {
        if (!func || func->blocks.empty()) return;
        // ============================================================================
        // Expand FILoadInst and FIStoreInst
        // ============================================================================
        for (auto& [bid, block] : func->blocks)
        {
            std::deque<MInstruction*> newInsts;
            for (auto* inst : block->insts)
            {
                if (!inst)
                    continue;

                if (inst->kind == InstKind::LSLOT)
                {
                    auto* fiLoad = static_cast<FILoadInst*>(inst);
                    int   offset = func->frameInfo.getSpillSlotOffset(fiLoad->frameIndex);

                    auto emitLoad = [&](Register base, int off) -> MInstruction* {
                        bool isFloat = fiLoad->dest.dt && fiLoad->dest.dt->dt == DataType::Type::FLOAT;
                        bool is32    = fiLoad->dest.dt && fiLoad->dest.dt->dl == DataType::Length::B32;
                        Operator op;
                        if (isFloat)
                            op = is32 ? Operator::FLW : Operator::FLD;
                        else
                            op = is32 ? Operator::LW : Operator::LD;
                        return createIInst(op, fiLoad->dest, base, off);
                    };

                    if (offset < -2048 || offset > 2047)
                    {
                        newInsts.push_back(createUInst(Operator::LI, PR::t0, offset));
                        newInsts.push_back(createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0));
                        newInsts.push_back(emitLoad(PR::t0, 0));
                    }
                    else
                    {
                        newInsts.push_back(emitLoad(PR::sp, offset));
                    }

                    delete fiLoad;
                    continue;
                }
                else if (inst->kind == InstKind::SSLOT)
                {
                    auto* fiStore = static_cast<FIStoreInst*>(inst);
                    int   offset  = func->frameInfo.getSpillSlotOffset(fiStore->frameIndex);

                    auto emitStore = [&](Register base, int off) -> MInstruction* {
                        bool isFloat = fiStore->src.dt && fiStore->src.dt->dt == DataType::Type::FLOAT;
                        bool is32    = fiStore->src.dt && fiStore->src.dt->dl == DataType::Length::B32;
                        Operator op;
                        if (isFloat)
                            op = is32 ? Operator::FSW : Operator::FSD;
                        else
                            op = is32 ? Operator::SW : Operator::SD;
                        return createSInst(op, fiStore->src, base, off);
                    };

                    if (offset < -2048 || offset > 2047)
                    {
                        newInsts.push_back(createUInst(Operator::LI, PR::t0, offset));
                        newInsts.push_back(createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0));
                        newInsts.push_back(emitStore(PR::t0, 0));
                    }
                    else
                    {
                        newInsts.push_back(emitStore(PR::sp, offset));
                    }

                    delete fiStore;
                    continue;
                }

                newInsts.push_back(inst);
            }
            block->insts.swap(newInsts);
        }

        // ============================================================================
        // Step 2: Expand MOVE pseudo-instructions
        // ============================================================================
        for (auto& [bid, block] : func->blocks)
        {
            std::deque<MInstruction*> newInsts;
            for (auto* inst : block->insts)
            {
                if (!inst)
                    continue;

                if (inst->kind == InstKind::MOVE)
                {
                    auto* mvInst = static_cast<MoveInst*>(inst);
                    auto* dstOp  = dynamic_cast<RegOperand*>(mvInst->dest);
                    if (!dstOp)
                    {
                        delete mvInst;
                        continue;
                    }

                    Register destReg = dstOp->reg;
                    if (destReg.isVreg)
                    {
                        delete mvInst;
                        continue;
                    }

                    MInstruction* replacement = nullptr;
                    if (auto* srcRegOp = dynamic_cast<RegOperand*>(mvInst->src))
                    {
                        if (destReg.dt && destReg.dt->dt == DataType::Type::FLOAT)
                            replacement = createR2Inst(Operator::FMV_S, destReg, srcRegOp->reg);
                        else
                            replacement = createIInst(Operator::ADDI, destReg, srcRegOp->reg, 0);
                    }
                    else if (auto* immOp = dynamic_cast<I32Operand*>(mvInst->src))
                    {
                        replacement = createUInst(Operator::LI, destReg, immOp->val);
                    }
                    else if (auto* fImmOp = dynamic_cast<F32Operand*>(mvInst->src))
                    {
                        uint32_t bits = 0;
                        static_assert(sizeof(bits) == sizeof(float), "size mismatch");
                        std::memcpy(&bits, &fImmOp->val, sizeof(float));
                        newInsts.push_back(createUInst(Operator::LI, PR::t0, static_cast<int>(bits)));
                        replacement = createR2Inst(Operator::FMV_W_X, destReg, PR::t0);
                    }

                    if (replacement) newInsts.push_back(replacement);
                    delete mvInst;
                    continue;
                }

                newInsts.push_back(inst);
            }
            block->insts.swap(newInsts);
        }
    }
}  // namespace BE::RV64::Passes::Lowering
