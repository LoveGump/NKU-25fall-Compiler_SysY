#include <middleend/visitor/utils/operand_replace_visitor.h>

namespace ME
{
    OperandReplaceVisitor::OperandReplaceVisitor(const std::unordered_map<size_t, Operand*>& regs) : replaceRegs(regs)
    {}

    void OperandReplaceVisitor::replaceOperand(Operand*& op)
    {
        if (!op || op->getType() != OperandType::REG) return;
        auto   it     = replaceRegs.find(op->getRegNum());
        if (it != replaceRegs.end()) {
            op = it->second;
        }
    }

    void OperandReplaceVisitor::visit(LoadInst& inst) { replaceOperand(inst.ptr); }

    void OperandReplaceVisitor::visit(StoreInst& inst)
    {
        replaceOperand(inst.ptr);
        replaceOperand(inst.val);
    }

    void OperandReplaceVisitor::visit(ArithmeticInst& inst)
    {
        replaceOperand(inst.lhs);
        replaceOperand(inst.rhs);
    }

    void OperandReplaceVisitor::visit(IcmpInst& inst)
    {
        replaceOperand(inst.lhs);
        replaceOperand(inst.rhs);
    }

    void OperandReplaceVisitor::visit(FcmpInst& inst)
    {
        replaceOperand(inst.lhs);
        replaceOperand(inst.rhs);
    }

    void OperandReplaceVisitor::visit(AllocaInst& inst) {}

    void OperandReplaceVisitor::visit(BrCondInst& inst) { replaceOperand(inst.cond); }

    void OperandReplaceVisitor::visit(BrUncondInst& inst) {}

    void OperandReplaceVisitor::visit(GlbVarDeclInst& inst) {}

    void OperandReplaceVisitor::visit(CallInst& inst)
    {
        for (auto& arg : inst.args) { replaceOperand(arg.second); }
    }

    void OperandReplaceVisitor::visit(FuncDeclInst& inst) {}

    void OperandReplaceVisitor::visit(FuncDefInst& inst) {}

    void OperandReplaceVisitor::visit(RetInst& inst) { replaceOperand(inst.res); }

    void OperandReplaceVisitor::visit(GEPInst& inst)
    {
        replaceOperand(inst.basePtr);
        for (auto*& idx : inst.idxs) replaceOperand(idx);
    }

    void OperandReplaceVisitor::visit(FP2SIInst& inst) { replaceOperand(inst.src); }

    void OperandReplaceVisitor::visit(SI2FPInst& inst) { replaceOperand(inst.src); }

    void OperandReplaceVisitor::visit(ZextInst& inst) { replaceOperand(inst.src); }

    void OperandReplaceVisitor::visit(PhiInst& inst)
    {
        for (auto& [label, val] : inst.incomingVals) replaceOperand(val);
    }
}  // namespace ME
