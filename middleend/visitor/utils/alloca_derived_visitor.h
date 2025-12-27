#ifndef __MIDDLEEND_VISITOR_UTILS_ALLOCA_DERIVED_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_ALLOCA_DERIVED_VISITOR_H__

#include <interfaces/middleend/ir_visitor.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace ME
{
    // 判断指令结果是否源自 alloca 指针的访问者
    // 使用真正的访问者模式，通过多态分发处理不同指令类型
    class AllocaDerivedVisitor : public InsVisitor_t<bool>
    {
      public:
        using RegChecker = std::function<bool(size_t)>;

        explicit AllocaDerivedVisitor(const RegChecker& checker);

        // 各指令类型的 visit 方法
        bool visit(AllocaInst& inst) override;
        bool visit(GEPInst& inst) override;
        bool visit(PhiInst& inst) override;
        bool visit(ZextInst& inst) override;
        bool visit(ArithmeticInst& inst) override;

        // 以下指令不会产生 alloca 派生的结果
        bool visit(LoadInst& inst) override;
        bool visit(StoreInst& inst) override;
        bool visit(IcmpInst& inst) override;
        bool visit(FcmpInst& inst) override;
        bool visit(BrCondInst& inst) override;
        bool visit(BrUncondInst& inst) override;
        bool visit(GlbVarDeclInst& inst) override;
        bool visit(CallInst& inst) override;
        bool visit(FuncDeclInst& inst) override;
        bool visit(FuncDefInst& inst) override;
        bool visit(RetInst& inst) override;
        bool visit(FP2SIInst& inst) override;
        bool visit(SI2FPInst& inst) override;

      private:
        const RegChecker& checkReg;

        bool isRegDerived(Operand* op) const;
    };

    // 封装类：管理记忆化缓存和循环检测，提供简洁的调用接口
    class AllocaDerivedChecker
    {
      public:
        explicit AllocaDerivedChecker(const std::unordered_map<size_t, Instruction*>& regDefs);

        bool isAllocaDerived(size_t regNum);
        bool isOperandDerived(Operand* op);
        bool hasAllocaDerivedArg(CallInst* call);

      private:
        const std::unordered_map<size_t, Instruction*>& regDefs;
        std::unordered_map<size_t, bool> memo;
        std::unordered_set<size_t> visiting;
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_ALLOCA_DERIVED_VISITOR_H__
