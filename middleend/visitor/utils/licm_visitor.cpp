#include <middleend/visitor/utils/licm_visitor.h>

namespace ME
{
    bool LICMScalarVisitor::visit(LoadInst& inst) { return false; }
    bool LICMScalarVisitor::visit(StoreInst& inst) { return false; }
    bool LICMScalarVisitor::visit(ArithmeticInst& inst) { return true; }
    bool LICMScalarVisitor::visit(IcmpInst& inst) { return true; }
    bool LICMScalarVisitor::visit(FcmpInst& inst) { return true; }
    bool LICMScalarVisitor::visit(AllocaInst& inst) { return false; }
    bool LICMScalarVisitor::visit(BrCondInst& inst) { return false; }
    bool LICMScalarVisitor::visit(BrUncondInst& inst) { return false; }
    bool LICMScalarVisitor::visit(GlbVarDeclInst& inst) { return false; }
    bool LICMScalarVisitor::visit(CallInst& inst) { return false; }
    bool LICMScalarVisitor::visit(FuncDeclInst& inst) { return false; }
    bool LICMScalarVisitor::visit(FuncDefInst& inst) { return false; }
    bool LICMScalarVisitor::visit(RetInst& inst) { return false; }
    bool LICMScalarVisitor::visit(GEPInst& inst) { return true; }
    bool LICMScalarVisitor::visit(FP2SIInst& inst) { return true; }
    bool LICMScalarVisitor::visit(SI2FPInst& inst) { return true; }
    bool LICMScalarVisitor::visit(ZextInst& inst) { return true; }
    bool LICMScalarVisitor::visit(PhiInst& inst) { return false; }

    bool LICMMemoryLikeVisitor::visit(LoadInst& inst) { return true; }
    bool LICMMemoryLikeVisitor::visit(StoreInst& inst) { return true; }
    bool LICMMemoryLikeVisitor::visit(ArithmeticInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(IcmpInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(FcmpInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(AllocaInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(BrCondInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(BrUncondInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(GlbVarDeclInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(CallInst& inst) { return true; }
    bool LICMMemoryLikeVisitor::visit(FuncDeclInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(FuncDefInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(RetInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(GEPInst& inst) { return true; }
    bool LICMMemoryLikeVisitor::visit(FP2SIInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(SI2FPInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(ZextInst& inst) { return false; }
    bool LICMMemoryLikeVisitor::visit(PhiInst& inst) { return false; }

    bool LICMSafeSpecVisitor::visit(LoadInst& inst)
    {
        // 只允许对全局变量的加载做提前执行
        if (!inst.ptr) return false;
        return inst.ptr->getType() == OperandType::GLOBAL;
    }
    bool LICMSafeSpecVisitor::visit(StoreInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(ArithmeticInst& inst)
    {
        // 除法/取模仅在除数为非零常量时允许提前执行
        if (inst.opcode == Operator::DIV || inst.opcode == Operator::MOD)
        {
            if (!inst.rhs) return false;
            if (inst.rhs->getType() == OperandType::IMMEI32)
            {
                auto* imm = static_cast<ImmeI32Operand*>(inst.rhs);
                return imm->value != 0;
            }
            return false;
        }
        if (inst.opcode == Operator::FDIV) return true;
        return true;
    }
    bool LICMSafeSpecVisitor::visit(IcmpInst& inst) { return true; }
    bool LICMSafeSpecVisitor::visit(FcmpInst& inst) { return true; }
    bool LICMSafeSpecVisitor::visit(AllocaInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(BrCondInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(BrUncondInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(GlbVarDeclInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(CallInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(FuncDeclInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(FuncDefInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(RetInst& inst) { return false; }
    bool LICMSafeSpecVisitor::visit(GEPInst& inst) { return true; }
    bool LICMSafeSpecVisitor::visit(FP2SIInst& inst) { return true; }
    bool LICMSafeSpecVisitor::visit(SI2FPInst& inst) { return true; }
    bool LICMSafeSpecVisitor::visit(ZextInst& inst) { return true; }
    bool LICMSafeSpecVisitor::visit(PhiInst& inst) { return false; }

    Operand* LICMGlobalLoadVisitor::visit(LoadInst& inst)
    {
        if (!inst.ptr) return nullptr;
        if (inst.ptr->getType() != OperandType::GLOBAL) return nullptr;
        return inst.ptr;
    }
    Operand* LICMGlobalLoadVisitor::visit(StoreInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(ArithmeticInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(IcmpInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(FcmpInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(AllocaInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(BrCondInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(BrUncondInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(GlbVarDeclInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(CallInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(FuncDeclInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(FuncDefInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(RetInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(GEPInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(FP2SIInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(SI2FPInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(ZextInst& inst) { return nullptr; }
    Operand* LICMGlobalLoadVisitor::visit(PhiInst& inst) { return nullptr; }

    Operand* LICMGlobalStoreVisitor::visit(LoadInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(StoreInst& inst)
    {
        if (!inst.ptr) return nullptr;
        if (inst.ptr->getType() != OperandType::GLOBAL) return nullptr;
        return inst.ptr;
    }
    Operand* LICMGlobalStoreVisitor::visit(ArithmeticInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(IcmpInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(FcmpInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(AllocaInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(BrCondInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(BrUncondInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(GlbVarDeclInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(CallInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(FuncDeclInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(FuncDefInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(RetInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(GEPInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(FP2SIInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(SI2FPInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(ZextInst& inst) { return nullptr; }
    Operand* LICMGlobalStoreVisitor::visit(PhiInst& inst) { return nullptr; }

    bool LICMCallVisitor::visit(LoadInst& inst) { return false; }
    bool LICMCallVisitor::visit(StoreInst& inst) { return false; }
    bool LICMCallVisitor::visit(ArithmeticInst& inst) { return false; }
    bool LICMCallVisitor::visit(IcmpInst& inst) { return false; }
    bool LICMCallVisitor::visit(FcmpInst& inst) { return false; }
    bool LICMCallVisitor::visit(AllocaInst& inst) { return false; }
    bool LICMCallVisitor::visit(BrCondInst& inst) { return false; }
    bool LICMCallVisitor::visit(BrUncondInst& inst) { return false; }
    bool LICMCallVisitor::visit(GlbVarDeclInst& inst) { return false; }
    bool LICMCallVisitor::visit(CallInst& inst) { return true; }
    bool LICMCallVisitor::visit(FuncDeclInst& inst) { return false; }
    bool LICMCallVisitor::visit(FuncDefInst& inst) { return false; }
    bool LICMCallVisitor::visit(RetInst& inst) { return false; }
    bool LICMCallVisitor::visit(GEPInst& inst) { return false; }
    bool LICMCallVisitor::visit(FP2SIInst& inst) { return false; }
    bool LICMCallVisitor::visit(SI2FPInst& inst) { return false; }
    bool LICMCallVisitor::visit(ZextInst& inst) { return false; }
    bool LICMCallVisitor::visit(PhiInst& inst) { return false; }

    bool LICMSelfCallVisitor::visit(LoadInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(StoreInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(ArithmeticInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(IcmpInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(FcmpInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(AllocaInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(BrCondInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(BrUncondInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(GlbVarDeclInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(CallInst& inst, const std::string& name) { return inst.funcName == name; }
    bool LICMSelfCallVisitor::visit(FuncDeclInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(FuncDefInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(RetInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(GEPInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(FP2SIInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(SI2FPInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(ZextInst& inst, const std::string& name) { return false; }
    bool LICMSelfCallVisitor::visit(PhiInst& inst, const std::string& name) { return false; }

    void LICMBranchReplaceVisitor::visit(LoadInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(StoreInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(ArithmeticInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(IcmpInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(FcmpInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(AllocaInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(BrCondInst& inst, Operand* oldLabel, Operand* newLabel)
    {
        if (inst.trueTar == oldLabel) inst.trueTar = newLabel;
        if (inst.falseTar == oldLabel) inst.falseTar = newLabel;
    }
    void LICMBranchReplaceVisitor::visit(BrUncondInst& inst, Operand* oldLabel, Operand* newLabel)
    {
        if (inst.target == oldLabel) inst.target = newLabel;
    }
    void LICMBranchReplaceVisitor::visit(GlbVarDeclInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(CallInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(FuncDeclInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(FuncDefInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(RetInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(GEPInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(FP2SIInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(SI2FPInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(ZextInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMBranchReplaceVisitor::visit(PhiInst& inst, Operand* oldLabel, Operand* newLabel) {}

    void LICMPhiReplaceVisitor::visit(LoadInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(StoreInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(ArithmeticInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(IcmpInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(FcmpInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(AllocaInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(BrCondInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(BrUncondInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(GlbVarDeclInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(CallInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(FuncDeclInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(FuncDefInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(RetInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(GEPInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(FP2SIInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(SI2FPInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(ZextInst& inst, Operand* oldLabel, Operand* newLabel) {}
    void LICMPhiReplaceVisitor::visit(PhiInst& inst, Operand* oldLabel, Operand* newLabel)
    {
        auto it = inst.incomingVals.find(oldLabel);
        if (it == inst.incomingVals.end()) return;
        Operand* val = it->second;
        inst.incomingVals.erase(it);
        inst.incomingVals[newLabel] = val;
    }
}  // namespace ME
