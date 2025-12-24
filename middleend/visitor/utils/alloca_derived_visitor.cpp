#include <middleend/visitor/utils/alloca_derived_visitor.h>

namespace ME
{
    AllocaDerivedVisitor::AllocaDerivedVisitor(const RegChecker& checker) : checkReg(checker) {}

    bool AllocaDerivedVisitor::isRegDerived(Operand* op) const
    {
        if (!op || op->getType() != OperandType::REG) return false;
        return checkReg(static_cast<RegOperand*>(op)->regNum);
    }

    bool AllocaDerivedVisitor::visit(LoadInst& inst)
    {
        if (inst.dt != DataType::PTR) return false;
        return isRegDerived(inst.ptr);
    }

    bool AllocaDerivedVisitor::visit(StoreInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(ArithmeticInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(IcmpInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(FcmpInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(AllocaInst& inst) { return true; }

    bool AllocaDerivedVisitor::visit(BrCondInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(BrUncondInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(GlbVarDeclInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(CallInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(FuncDeclInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(FuncDefInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(RetInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(GEPInst& inst) { return isRegDerived(inst.basePtr); }

    bool AllocaDerivedVisitor::visit(FP2SIInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(SI2FPInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(ZextInst& inst) { return false; }

    bool AllocaDerivedVisitor::visit(PhiInst& inst)
    {
        for (auto& [label, val] : inst.incomingVals)
        {
            if (isRegDerived(val)) return true;
        }
        return false;
    }
}  // namespace ME
