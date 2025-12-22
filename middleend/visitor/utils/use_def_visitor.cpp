#include <middleend/visitor/utils/use_def_visitor.h>

namespace ME
{
    // UseCollector 实现
    UseCollector::UseCollector(std::map<size_t, int>& counts) : useCounts(counts) {}

    void UseCollector::addUse(Operand* op)
    {
        if (op && op->getType() == OperandType::REG) { useCounts[static_cast<RegOperand*>(op)->regNum]++; }
    }

    void UseCollector::visit(LoadInst& inst) { addUse(inst.ptr); }
    void UseCollector::visit(StoreInst& inst)
    {
        addUse(inst.ptr);
        addUse(inst.val);
    }
    void UseCollector::visit(ArithmeticInst& inst)
    {
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UseCollector::visit(IcmpInst& inst)
    {
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UseCollector::visit(FcmpInst& inst)
    {
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UseCollector::visit(AllocaInst& inst) {}
    void UseCollector::visit(BrCondInst& inst) { addUse(inst.cond); }
    void UseCollector::visit(BrUncondInst& inst) {}
    void UseCollector::visit(GlbVarDeclInst& inst) {}
    void UseCollector::visit(CallInst& inst)
    {
        for (auto& arg : inst.args) addUse(arg.second);
    }
    void UseCollector::visit(FuncDeclInst& inst) {}
    void UseCollector::visit(FuncDefInst& inst) {}
    void UseCollector::visit(RetInst& inst) { addUse(inst.res); }
    void UseCollector::visit(GEPInst& inst)
    {
        addUse(inst.basePtr);
        for (auto* idx : inst.idxs) addUse(idx);
    }
    void UseCollector::visit(FP2SIInst& inst) { addUse(inst.src); }
    void UseCollector::visit(SI2FPInst& inst) { addUse(inst.src); }
    void UseCollector::visit(ZextInst& inst) { addUse(inst.src); }
    void UseCollector::visit(PhiInst& inst)
    {
        for (auto& [label, val] : inst.incomingVals) addUse(val);
    }

    // DefCollector Implementation
    size_t DefCollector::getReg(Operand* op)
    {
        if (op && op->getType() == OperandType::REG) { return static_cast<RegOperand*>(op)->regNum; }
        return 0;
    }

    void DefCollector::visit(LoadInst& inst) { defReg = getReg(inst.res); }
    void DefCollector::visit(StoreInst& inst) { defReg = 0; }
    void DefCollector::visit(ArithmeticInst& inst) { defReg = getReg(inst.res); }
    void DefCollector::visit(IcmpInst& inst) { defReg = getReg(inst.res); }
    void DefCollector::visit(FcmpInst& inst) { defReg = getReg(inst.res); }
    void DefCollector::visit(AllocaInst& inst) { defReg = getReg(inst.res); }
    void DefCollector::visit(BrCondInst& inst) { defReg = 0; }
    void DefCollector::visit(BrUncondInst& inst) { defReg = 0; }
    void DefCollector::visit(GlbVarDeclInst& inst) { defReg = 0; }
    void DefCollector::visit(CallInst& inst) { defReg = getReg(inst.res); }
    void DefCollector::visit(FuncDeclInst& inst) { defReg = 0; }
    void DefCollector::visit(FuncDefInst& inst) { defReg = 0; }
    void DefCollector::visit(RetInst& inst) { defReg = 0; }
    void DefCollector::visit(GEPInst& inst) { defReg = getReg(inst.res); }
    void DefCollector::visit(FP2SIInst& inst) { defReg = getReg(inst.dest); }
    void DefCollector::visit(SI2FPInst& inst) { defReg = getReg(inst.dest); }
    void DefCollector::visit(ZextInst& inst) { defReg = getReg(inst.dest); }
    void DefCollector::visit(PhiInst& inst) { defReg = getReg(inst.res); }
}  // namespace ME
