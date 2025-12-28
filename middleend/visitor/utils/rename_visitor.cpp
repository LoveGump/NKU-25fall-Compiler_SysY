#include <middleend/visitor/utils/rename_visitor.h>
#include <middleend/module/ir_operand.h>

namespace ME
{
    void renameReg(Operand*& operand, RegMap& renameMap)
    {
        if (!operand || operand->getType() != OperandType::REG) return;

        auto it = renameMap.find(operand->getRegNum());
        if (it == renameMap.end()) return;
        operand = getRegOperand(it->second);
    }

    void RegRename::visit(LoadInst& inst, RegMap& rm)
    {
        renameReg(inst.ptr, rm);
        renameReg(inst.res, rm);
    }

    void RegRename::visit(StoreInst& inst, RegMap& rm)
    {
        renameReg(inst.ptr, rm);
        renameReg(inst.val, rm);
    }

    void RegRename::visit(ArithmeticInst& inst, RegMap& rm)
    {
        renameReg(inst.lhs, rm);
        renameReg(inst.rhs, rm);
        renameReg(inst.res, rm);
    }

    void RegRename::visit(IcmpInst& inst, RegMap& rm)
    {
        renameReg(inst.lhs, rm);
        renameReg(inst.rhs, rm);
        renameReg(inst.res, rm);
    }

    void RegRename::visit(FcmpInst& inst, RegMap& rm)
    {
        renameReg(inst.lhs, rm);
        renameReg(inst.rhs, rm);
        renameReg(inst.res, rm);
    }

    void RegRename::visit(AllocaInst& inst, RegMap& rm) { renameReg(inst.res, rm); }

    void RegRename::visit(BrCondInst& inst, RegMap& rm) { renameReg(inst.cond, rm); }

    void RegRename::visit(BrUncondInst& inst, RegMap& rm)
    {
        (void)inst;
        (void)rm;
    }

    void RegRename::visit(GlbVarDeclInst& inst, RegMap& rm) { renameReg(inst.init, rm); }

    void RegRename::visit(CallInst& inst, RegMap& rm)
    {
        for (auto& arg : inst.args) renameReg(arg.second, rm);
        renameReg(inst.res, rm);
    }

    void RegRename::visit(FuncDeclInst& inst, RegMap& rm)
    {
        (void)inst;
        (void)rm;
    }

    void RegRename::visit(FuncDefInst& inst, RegMap& rm)
    {
        for (auto& arg : inst.argRegs) renameReg(arg.second, rm);
    }

    void RegRename::visit(RetInst& inst, RegMap& rm) { renameReg(inst.res, rm); }

    void RegRename::visit(GEPInst& inst, RegMap& rm)
    {
        renameReg(inst.basePtr, rm);
        renameReg(inst.res, rm);
        for (auto& idx : inst.idxs) renameReg(idx, rm);
    }

    void RegRename::visit(FP2SIInst& inst, RegMap& rm)
    {
        renameReg(inst.src, rm);
        renameReg(inst.dest, rm);
    }

    void RegRename::visit(SI2FPInst& inst, RegMap& rm)
    {
        renameReg(inst.src, rm);
        renameReg(inst.dest, rm);
    }

    void RegRename::visit(ZextInst& inst, RegMap& rm)
    {
        renameReg(inst.src, rm);
        renameReg(inst.dest, rm);
    }

    void RegRename::visit(PhiInst& inst, RegMap& rm)
    {
        renameReg(inst.res, rm);
        std::map<Operand*, Operand*> newIncomingVals;
        for (auto& [label, val] : inst.incomingVals)
        {
            Operand* newVal = val;
            renameReg(newVal, rm);
            newIncomingVals[label] = newVal;
        }
        inst.incomingVals = newIncomingVals;
    }

    // Operand 重命名辅助函数，将操作数根据重命名映射进行替换
    void renameOperand(Operand*& operand, OperandMap& renameMap)
    {
        if (!operand || operand->getType() != OperandType::REG) return;

        // 获取寄存器操作数
        auto it = renameMap.find(operand->getRegNum());  // 如果在映射中找到对应的重命名操作数
        if (it == renameMap.end()) return;
        operand = it->second;  // 替换为重命名后的操作数
    }

    void OperandRename::visit(LoadInst& inst, OperandMap& rm)
    {
        renameOperand(inst.ptr, rm);
        renameOperand(inst.res, rm);
    }

    void OperandRename::visit(StoreInst& inst, OperandMap& rm)
    {
        renameOperand(inst.ptr, rm);
        renameOperand(inst.val, rm);
    }

    void OperandRename::visit(ArithmeticInst& inst, OperandMap& rm)
    {
        renameOperand(inst.lhs, rm);
        renameOperand(inst.rhs, rm);
        renameOperand(inst.res, rm);
    }

    void OperandRename::visit(IcmpInst& inst, OperandMap& rm)
    {
        renameOperand(inst.lhs, rm);
        renameOperand(inst.rhs, rm);
        renameOperand(inst.res, rm);
    }

    void OperandRename::visit(FcmpInst& inst, OperandMap& rm)
    {
        renameOperand(inst.lhs, rm);
        renameOperand(inst.rhs, rm);
        renameOperand(inst.res, rm);
    }

    void OperandRename::visit(AllocaInst& inst, OperandMap& rm) { renameOperand(inst.res, rm); }

    void OperandRename::visit(BrCondInst& inst, OperandMap& rm) { renameOperand(inst.cond, rm); }

    void OperandRename::visit(BrUncondInst& inst, OperandMap& rm)
    {
        (void)inst;
        (void)rm;
    }

    void OperandRename::visit(GlbVarDeclInst& inst, OperandMap& rm) { renameOperand(inst.init, rm); }

    void OperandRename::visit(CallInst& inst, OperandMap& rm)
    {
        for (auto& arg : inst.args) renameOperand(arg.second, rm);
        renameOperand(inst.res, rm);
    }

    void OperandRename::visit(FuncDeclInst& inst, OperandMap& rm)
    {
        (void)inst;
        (void)rm;
    }

    void OperandRename::visit(FuncDefInst& inst, OperandMap& rm)
    {
        for (auto& arg : inst.argRegs) renameOperand(arg.second, rm);
    }

    void OperandRename::visit(RetInst& inst, OperandMap& rm) { renameOperand(inst.res, rm); }

    void OperandRename::visit(GEPInst& inst, OperandMap& rm)
    {
        renameOperand(inst.basePtr, rm);
        renameOperand(inst.res, rm);
        for (auto& idx : inst.idxs) renameOperand(idx, rm);
    }

    void OperandRename::visit(FP2SIInst& inst, OperandMap& rm)
    {
        renameOperand(inst.src, rm);
        renameOperand(inst.dest, rm);
    }

    void OperandRename::visit(SI2FPInst& inst, OperandMap& rm)
    {
        renameOperand(inst.src, rm);
        renameOperand(inst.dest, rm);
    }

    void OperandRename::visit(ZextInst& inst, OperandMap& rm)
    {
        renameOperand(inst.src, rm);
        renameOperand(inst.dest, rm);
    }

    void OperandRename::visit(PhiInst& inst, OperandMap& rm)
    {
        renameOperand(inst.res, rm);
        std::map<Operand*, Operand*> newIncomingVals;
        for (auto& [label, val] : inst.incomingVals)
        {
            Operand* newVal = val;
            renameOperand(newVal, rm);
            newIncomingVals[label] = newVal;
        }
        inst.incomingVals = newIncomingVals;
    }
}  // namespace ME
