#include <middleend/visitor/utils/alloca_derived_visitor.h>

namespace ME
{
    // AllocaDerivedVisitor 实现
    AllocaDerivedVisitor::AllocaDerivedVisitor(const RegChecker& checker) : checkReg(checker) {}

    bool AllocaDerivedVisitor::isRegDerived(Operand* op) const
    {
        if (!op || op->getType() != OperandType::REG) return false;
        return checkReg(op->getRegNum());
    }

    bool AllocaDerivedVisitor::visit(AllocaInst& inst) { return true; }

    bool AllocaDerivedVisitor::visit(GEPInst& inst) { return isRegDerived(inst.basePtr); }

    bool AllocaDerivedVisitor::visit(PhiInst& inst)
    {
        for (auto& [label, val] : inst.incomingVals)
        {
            if (isRegDerived(val)) return true;
        }
        return false;
    }

    bool AllocaDerivedVisitor::visit(ZextInst& inst) { return isRegDerived(inst.src); }

    bool AllocaDerivedVisitor::visit(ArithmeticInst& inst)
    {
        return isRegDerived(inst.lhs) || isRegDerived(inst.rhs);
    }

    bool AllocaDerivedVisitor::visit(LoadInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(StoreInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(IcmpInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(FcmpInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(BrCondInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(BrUncondInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(GlbVarDeclInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(CallInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(FuncDeclInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(FuncDefInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(RetInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(FP2SIInst& inst) { return false; }
    bool AllocaDerivedVisitor::visit(SI2FPInst& inst) { return false; }

    // AllocaDerivedChecker 实现
    AllocaDerivedChecker::AllocaDerivedChecker(const std::unordered_map<size_t, Instruction*>& regDefs)
        : regDefs(regDefs) {}

    bool AllocaDerivedChecker::isAllocaDerived(size_t regNum)
    {
        // 检查缓存
        auto memoIt = memo.find(regNum);
        if (memoIt != memo.end()) return memoIt->second;

        // 检查循环依赖
        if (!visiting.insert(regNum).second) return false;

        bool derived = false;
        auto defIt = regDefs.find(regNum);
        if (defIt != regDefs.end())
        {
            // 创建 visitor，回调到本对象的 isAllocaDerived
            AllocaDerivedVisitor::RegChecker checker = [this](size_t reg) {
                return this->isAllocaDerived(reg);
            };
            AllocaDerivedVisitor visitor(checker);
            derived = apply(visitor, *defIt->second);
        }

        visiting.erase(regNum);
        memo[regNum] = derived;
        return derived;
    }

    bool AllocaDerivedChecker::isOperandDerived(Operand* op)
    {
        if (!op || op->getType() != OperandType::REG) return false;
        return isAllocaDerived(op->getRegNum());
    }

    bool AllocaDerivedChecker::hasAllocaDerivedArg(CallInst* call)
    {
        if (!call) return false;
        for (auto& arg : call->args)
        {
            Operand* op = arg.second;
            if (!op || op->getType() != OperandType::REG) continue;
            if (isAllocaDerived(op->getRegNum())) return true;
        }
        return false;
    }
}  // namespace ME
