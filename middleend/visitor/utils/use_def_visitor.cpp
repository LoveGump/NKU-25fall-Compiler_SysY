#include <middleend/visitor/utils/use_def_visitor.h>

namespace ME
{
    // UseCollector 实现
    UseCollector::UseCollector(std::map<size_t, int>& counts) : useCounts(counts) {}

    void UseCollector::addUse(Operand* op)
    {
        // 如果是寄存器操作数，则增加其使用次数
        if (op && op->getType() == OperandType::REG) { useCounts[op->getRegNum()]++; }
    }

    void UseCollector::visit(LoadInst& inst)
    {
        // load指令，使用了ptr寄存器，存放结果的res寄存器为定义，不计入使用
        addUse(inst.ptr);
    }
    void UseCollector::visit(StoreInst& inst)
    {
        // store指令，使用了ptr和val寄存器，将val存进ptr地址，都记为使用
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
        // icmp指令，使用了lhs和rhs寄存器
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UseCollector::visit(FcmpInst& inst)
    {
        // fcmp指令，使用了lhs和rhs寄存器
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UseCollector::visit(AllocaInst& inst)
    {
        // alloca指令，没有使用寄存器
    }
    void UseCollector::visit(BrCondInst& inst)
    {
        // br_cond指令，使用了条件寄存器
        addUse(inst.cond);
    }
    void UseCollector::visit(BrUncondInst& inst) {}
    void UseCollector::visit(GlbVarDeclInst& inst) {}
    void UseCollector::visit(CallInst& inst)
    {
        // 调用指令，使用了所有参数寄存器
        for (auto& arg : inst.args) { addUse(arg.second); }
    }
    void UseCollector::visit(FuncDeclInst& inst) {}
    void UseCollector::visit(FuncDefInst& inst) {}
    void UseCollector::visit(RetInst& inst)
    {
        // ret指令，使用了返回值寄存器（若有）
        addUse(inst.res);
    }
    void UseCollector::visit(GEPInst& inst)
    {
        addUse(inst.basePtr);
        for (auto* idx : inst.idxs) { addUse(idx); }
    }
    void UseCollector::visit(FP2SIInst& inst) { addUse(inst.src); }
    void UseCollector::visit(SI2FPInst& inst) { addUse(inst.src); }
    void UseCollector::visit(ZextInst& inst) { addUse(inst.src); }
    void UseCollector::visit(PhiInst& inst)
    {
        for (auto& [label, val] : inst.incomingVals) { addUse(val); }
    }

    // 收集定义的寄存器编号，若无定义则为0
    size_t DefCollector::getReg(Operand* op)
    {
        if (op && op->getType() == OperandType::REG)
        {
            // 返回寄存器编号
            return op->getRegNum();
        }
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

    // 收集使用指定寄存器的指令列表
    void UserCollector::addUse(Operand* op)
    {
        if (op && op->getType() == OperandType::REG)
        {
            userMap[op->getRegNum()].push_back(currentInst);
        }
    }

    void UserCollector::visit(LoadInst& inst)
    {
        currentInst = &inst;
        addUse(inst.ptr);
    }
    void UserCollector::visit(StoreInst& inst)
    {
        currentInst = &inst;
        addUse(inst.ptr);
        addUse(inst.val);
    }
    void UserCollector::visit(ArithmeticInst& inst)
    {
        currentInst = &inst;
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UserCollector::visit(IcmpInst& inst)
    {
        currentInst = &inst;
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UserCollector::visit(FcmpInst& inst)
    {
        currentInst = &inst;
        addUse(inst.lhs);
        addUse(inst.rhs);
    }
    void UserCollector::visit(AllocaInst& inst)
    { /* no uses */
    }
    void UserCollector::visit(BrCondInst& inst)
    {
        currentInst = &inst;
        addUse(inst.cond);
    }
    void UserCollector::visit(BrUncondInst& inst)
    { /* no uses */
    }
    void UserCollector::visit(GlbVarDeclInst& inst)
    { /* no uses */
    }
    void UserCollector::visit(CallInst& inst)
    {
        currentInst = &inst;
        for (auto& arg : inst.args) addUse(arg.second);
    }
    void UserCollector::visit(FuncDeclInst& inst) {}
    void UserCollector::visit(FuncDefInst& inst) {}
    void UserCollector::visit(RetInst& inst)
    {
        currentInst = &inst;
        addUse(inst.res);
    }
    void UserCollector::visit(GEPInst& inst)
    {
        currentInst = &inst;
        addUse(inst.basePtr);
        for (auto* idx : inst.idxs) addUse(idx);
    }
    void UserCollector::visit(FP2SIInst& inst)
    {
        currentInst = &inst;
        addUse(inst.src);
    }
    void UserCollector::visit(SI2FPInst& inst)
    {
        currentInst = &inst;
        addUse(inst.src);
    }
    void UserCollector::visit(ZextInst& inst)
    {
        currentInst = &inst;
        addUse(inst.src);
    }
    void UserCollector::visit(PhiInst& inst)
    {
        currentInst = &inst;
        for (auto& [label, val] : inst.incomingVals) addUse(val);
    }
}  // namespace ME
