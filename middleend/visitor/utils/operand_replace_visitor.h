#ifndef __MIDDLEEND_VISITOR_UTILS_OPERAND_REPLACE_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_OPERAND_REPLACE_VISITOR_H__

#include <interfaces/middleend/ir_visitor.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <unordered_map>

namespace ME
{
    // 统一替换寄存器操作数
    class OperandReplaceVisitor : public InsVisitor_t<void>
    {
      public:
        const std::unordered_map<size_t, Operand*>& replaceRegs;

        explicit OperandReplaceVisitor(const std::unordered_map<size_t, Operand*>& regs);

        void visit(LoadInst& inst) override;
        void visit(StoreInst& inst) override;
        void visit(ArithmeticInst& inst) override;
        void visit(IcmpInst& inst) override;
        void visit(FcmpInst& inst) override;
        void visit(AllocaInst& inst) override;
        void visit(BrCondInst& inst) override;
        void visit(BrUncondInst& inst) override;
        void visit(GlbVarDeclInst& inst) override;
        void visit(CallInst& inst) override;
        void visit(FuncDeclInst& inst) override;
        void visit(FuncDefInst& inst) override;
        void visit(RetInst& inst) override;
        void visit(GEPInst& inst) override;
        void visit(FP2SIInst& inst) override;
        void visit(SI2FPInst& inst) override;
        void visit(ZextInst& inst) override;
        void visit(PhiInst& inst) override;

      private:
        void replaceOperand(Operand*& op);
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_OPERAND_REPLACE_VISITOR_H__
