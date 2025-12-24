#ifndef __MIDDLEEND_VISITOR_UTILS_ALLOCA_DERIVED_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_ALLOCA_DERIVED_VISITOR_H__

#include <interfaces/middleend/ir_visitor.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <functional>

namespace ME
{
    // 判断指令结果是否基于 alloca 指针派生的访问者
    class AllocaDerivedVisitor : public InsVisitor_t<bool>
    {
      public:
        using RegChecker = std::function<bool(size_t)>;

        explicit AllocaDerivedVisitor(const RegChecker& checker);

        bool visit(LoadInst& inst) override;
        bool visit(StoreInst& inst) override;
        bool visit(ArithmeticInst& inst) override;
        bool visit(IcmpInst& inst) override;
        bool visit(FcmpInst& inst) override;
        bool visit(AllocaInst& inst) override;
        bool visit(BrCondInst& inst) override;
        bool visit(BrUncondInst& inst) override;
        bool visit(GlbVarDeclInst& inst) override;
        bool visit(CallInst& inst) override;
        bool visit(FuncDeclInst& inst) override;
        bool visit(FuncDefInst& inst) override;
        bool visit(RetInst& inst) override;
        bool visit(GEPInst& inst) override;
        bool visit(FP2SIInst& inst) override;
        bool visit(SI2FPInst& inst) override;
        bool visit(ZextInst& inst) override;
        bool visit(PhiInst& inst) override;

      private:
        const RegChecker& checkReg;
        bool              isRegDerived(Operand* op) const;
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_ALLOCA_DERIVED_VISITOR_H__
