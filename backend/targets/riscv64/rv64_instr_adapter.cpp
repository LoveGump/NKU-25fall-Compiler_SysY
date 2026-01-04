#include <backend/targets/riscv64/rv64_instr_adapter.h>
#include <backend/targets/riscv64/rv64_defs.h>
#include <algorithm>

namespace BE::Targeting::RV64
{
    using namespace BE::RV64;

    bool InstrAdapter::isCall(BE::MInstruction* inst) const
    {
        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return false;
        return ri->op == Operator::CALL;
    }

    bool InstrAdapter::isReturn(BE::MInstruction* inst) const
    {
        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return false;
        if (ri->op == Operator::RET) return true;
        // Treat "jalr x0, ra, 0" as return as well
        if (ri->op == Operator::JALR && !ri->rd.isVreg && ri->rd.rId == 0 && !ri->rs1.isVreg && ri->rs1.rId == 1 &&
            ri->imme == 0)
            return true;
        return false;
    }

    bool InstrAdapter::isUncondBranch(BE::MInstruction* inst) const
    {
        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return false;
        // JAL with rd=x0 is unconditional jump (j pseudo-instruction)
        if (ri->op == Operator::JAL && ri->rd.rId == 0 && !ri->rd.isVreg) return true;
        return false;
    }

    bool InstrAdapter::isCondBranch(BE::MInstruction* inst) const
    {
        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return false;
        switch (ri->op)
        {
            case Operator::BEQ:
            case Operator::BNE:
            case Operator::BLT:
            case Operator::BGE:
            case Operator::BLTU:
            case Operator::BGEU:
            case Operator::BGT:
            case Operator::BLE:
            case Operator::BGTU:
            case Operator::BLEU: return true;
            default: return false;
        }
    }

    int InstrAdapter::extractBranchTarget(BE::MInstruction* inst) const
    {
        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return -1;
        // 返回跳转目标标签 ID
        if (ri->use_label) return ri->label.jmp_label;
        return -1;
    }

    void InstrAdapter::enumUses(BE::MInstruction* inst, std::vector<BE::Register>& out) const
    {
        out.clear();
        // Handle pseudo instructions
        if (inst->kind == BE::InstKind::PHI)
        {
            auto* phi = dynamic_cast<BE::PhiInst*>(inst);
            if (phi)
            {
                for (auto& [label, op] : phi->incomingVals)
                {
                    if (op && op->ot == BE::Operand::Type::REG)
                    {
                        auto* regOp = dynamic_cast<BE::RegOperand*>(op);
                        if (regOp && regOp->reg.isVreg) out.push_back(regOp->reg);
                    }
                }
            }
            return;
        }
        if (inst->kind == BE::InstKind::MOVE)
        {
            auto* mv = dynamic_cast<BE::MoveInst*>(inst);
            if (mv && mv->src && mv->src->ot == BE::Operand::Type::REG)
            {
                auto* regOp = dynamic_cast<BE::RegOperand*>(mv->src);
                if (regOp && regOp->reg.isVreg) out.push_back(regOp->reg);
            }
            return;
        }
        if (inst->kind == BE::InstKind::SSLOT)
        {
            auto* fi = dynamic_cast<BE::FIStoreInst*>(inst);
            if (fi && fi->src.isVreg) out.push_back(fi->src);
            return;
        }

        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return;

        // Determine uses based on instruction type
        // R-type: uses rs1, rs2
        // I-type (non-load): uses rs1; Load: uses rs1
        // S-type: uses rs1 (base), val (rs2 for address calc, but val is the store source)
        // B-type: uses rs1, rs2
        // CALL: may use argument registers (handled implicitly)

        // For RV64 Instr, rs1 and rs2 are the source operands
        if (ri->rs1.isVreg) out.push_back(ri->rs1);
        if (ri->rs2.isVreg) out.push_back(ri->rs2);
    }

    void InstrAdapter::enumDefs(BE::MInstruction* inst, std::vector<BE::Register>& out) const
    {
        out.clear();
        // Handle pseudo instructions
        if (inst->kind == BE::InstKind::PHI)
        {
            auto* phi = dynamic_cast<BE::PhiInst*>(inst);
            if (phi && phi->resReg.isVreg) out.push_back(phi->resReg);
            return;
        }
        if (inst->kind == BE::InstKind::MOVE)
        {
            auto* mv = dynamic_cast<BE::MoveInst*>(inst);
            if (mv && mv->dest && mv->dest->ot == BE::Operand::Type::REG)
            {
                auto* regOp = dynamic_cast<BE::RegOperand*>(mv->dest);
                if (regOp && regOp->reg.isVreg) out.push_back(regOp->reg);
            }
            return;
        }
        if (inst->kind == BE::InstKind::LSLOT)
        {
            auto* fi = dynamic_cast<BE::FILoadInst*>(inst);
            if (fi && fi->dest.isVreg) out.push_back(fi->dest);
            return;
        }

        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return;

        // S-type and B-type instructions do not define registers
        switch (ri->op)
        {
            case Operator::SW:
            case Operator::SD:
            case Operator::FSW:
            case Operator::FSD:
            case Operator::BEQ:
            case Operator::BNE:
            case Operator::BLT:
            case Operator::BGE:
            case Operator::BLTU:
            case Operator::BGEU:
            case Operator::BGT:
            case Operator::BLE:
            case Operator::BGTU:
            case Operator::BLEU:
            case Operator::RET: return;  // No definition
            default: break;
        }

        // Other instructions define rd
        if (ri->rd.isVreg) out.push_back(ri->rd);
    }

    static void replaceReg(BE::Register& slot, const BE::Register& from, const BE::Register& to)
    {
        if (slot == from) slot = to;
    }

    void InstrAdapter::replaceUse(BE::MInstruction* inst, const BE::Register& from, const BE::Register& to) const
    {
        // Handle pseudo instructions
        if (inst->kind == BE::InstKind::PHI)
        {
            auto* phi = dynamic_cast<BE::PhiInst*>(inst);
            if (phi)
            {
                for (auto& [label, op] : phi->incomingVals)
                {
                    if (op && op->ot == BE::Operand::Type::REG)
                    {
                        auto* regOp = dynamic_cast<BE::RegOperand*>(op);
                        if (regOp) replaceReg(regOp->reg, from, to);
                    }
                }
            }
            return;
        }
        if (inst->kind == BE::InstKind::MOVE)
        {
            auto* mv = dynamic_cast<BE::MoveInst*>(inst);
            if (mv && mv->src && mv->src->ot == BE::Operand::Type::REG)
            {
                auto* regOp = dynamic_cast<BE::RegOperand*>(mv->src);
                if (regOp) replaceReg(regOp->reg, from, to);
            }
            return;
        }
        if (inst->kind == BE::InstKind::SSLOT)
        {
            auto* fi = dynamic_cast<BE::FIStoreInst*>(inst);
            if (fi) replaceReg(fi->src, from, to);
            return;
        }

        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return;
        replaceReg(ri->rs1, from, to);
        replaceReg(ri->rs2, from, to);
    }

    void InstrAdapter::replaceDef(BE::MInstruction* inst, const BE::Register& from, const BE::Register& to) const
    {
        // Handle pseudo instructions
        if (inst->kind == BE::InstKind::PHI)
        {
            auto* phi = dynamic_cast<BE::PhiInst*>(inst);
            if (phi) replaceReg(phi->resReg, from, to);
            return;
        }
        if (inst->kind == BE::InstKind::MOVE)
        {
            auto* mv = dynamic_cast<BE::MoveInst*>(inst);
            if (mv && mv->dest && mv->dest->ot == BE::Operand::Type::REG)
            {
                auto* regOp = dynamic_cast<BE::RegOperand*>(mv->dest);
                if (regOp) replaceReg(regOp->reg, from, to);
            }
            return;
        }
        if (inst->kind == BE::InstKind::LSLOT)
        {
            auto* fi = dynamic_cast<BE::FILoadInst*>(inst);
            if (fi) replaceReg(fi->dest, from, to);
            return;
        }

        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return;
        replaceReg(ri->rd, from, to);
    }

    void InstrAdapter::enumPhysRegs(BE::MInstruction* inst, std::vector<BE::Register>& out) const
    {
        out.clear();
        auto* ri = dynamic_cast<Instr*>(inst);
        if (!ri) return;

        // Collect physical (non-virtual) registers
        if (!ri->rd.isVreg && ri->rd.rId != 0) out.push_back(ri->rd);
        if (!ri->rs1.isVreg && ri->rs1.rId != 0) out.push_back(ri->rs1);
        if (!ri->rs2.isVreg && ri->rs2.rId != 0) out.push_back(ri->rs2);
    }

    void InstrAdapter::insertReloadBefore(
        BE::Block* block, std::deque<BE::MInstruction*>::iterator it, const BE::Register& physReg, int frameIndex) const
    {
        // Insert FILoadInst before the current instruction
        auto* reload = new BE::FILoadInst(physReg, frameIndex, "reload from spill slot");
        block->insts.insert(it, reload);
    }

    void InstrAdapter::insertSpillAfter(
        BE::Block* block, std::deque<BE::MInstruction*>::iterator it, const BE::Register& physReg, int frameIndex) const
    {
        // Insert FIStoreInst after the current instruction
        auto* spill = new BE::FIStoreInst(physReg, frameIndex, "spill to spill slot");
        block->insts.insert(std::next(it), spill);
    }
}  // namespace BE::Targeting::RV64
