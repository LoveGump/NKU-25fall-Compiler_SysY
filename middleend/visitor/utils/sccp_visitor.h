#ifndef __MIDDLEEND_VISITOR_UTILS_SCCP_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_SCCP_VISITOR_H__

#include <middleend/pass/sccp.h>
#include <interfaces/middleend/ir_visitor.h>

namespace ME
{
    // SCCP 指令求值：驱动格值更新与可达边传播
    class SCCPEvalVisitor : public InsVisitor_t<void, SCCPPass&, Block*>
    {
      public:
        void visit(LoadInst& inst, SCCPPass& pass, Block* block) override;
        void visit(StoreInst& inst, SCCPPass& pass, Block* block) override;
        void visit(ArithmeticInst& inst, SCCPPass& pass, Block* block) override;
        void visit(IcmpInst& inst, SCCPPass& pass, Block* block) override;
        void visit(FcmpInst& inst, SCCPPass& pass, Block* block) override;
        void visit(AllocaInst& inst, SCCPPass& pass, Block* block) override;
        void visit(BrCondInst& inst, SCCPPass& pass, Block* block) override;
        void visit(BrUncondInst& inst, SCCPPass& pass, Block* block) override;
        void visit(GlbVarDeclInst& inst, SCCPPass& pass, Block* block) override;
        void visit(CallInst& inst, SCCPPass& pass, Block* block) override;
        void visit(FuncDeclInst& inst, SCCPPass& pass, Block* block) override;
        void visit(FuncDefInst& inst, SCCPPass& pass, Block* block) override;
        void visit(RetInst& inst, SCCPPass& pass, Block* block) override;
        void visit(GEPInst& inst, SCCPPass& pass, Block* block) override;
        void visit(FP2SIInst& inst, SCCPPass& pass, Block* block) override;
        void visit(SI2FPInst& inst, SCCPPass& pass, Block* block) override;
        void visit(ZextInst& inst, SCCPPass& pass, Block* block) override;
        void visit(PhiInst& inst, SCCPPass& pass, Block* block) override;

      private:
        void updateValue(SCCPPass& pass, Operand* dest, const SCCPPass::LatticeVal& val);
        void markEdgeReachable(SCCPPass& pass, size_t from, size_t to);
        SCCPPass::LatticeVal makeUndef() const;
        SCCPPass::LatticeVal makeOverdefined() const;
        SCCPPass::LatticeVal makeConstInt(int value) const;
        SCCPPass::LatticeVal makeConstFloat(float value) const;
        SCCPPass::LatticeVal getValue(SCCPPass& pass, Operand* op) const;
        SCCPPass::LatticeVal mergeValue(const SCCPPass::LatticeVal& lhs,
                                        const SCCPPass::LatticeVal& rhs) const;
        SCCPPass::LatticeVal evalArithmetic(SCCPPass& pass, ArithmeticInst& inst) const;
        SCCPPass::LatticeVal evalIcmp(SCCPPass& pass, IcmpInst& inst) const;
        SCCPPass::LatticeVal evalFcmp(SCCPPass& pass, FcmpInst& inst) const;
        SCCPPass::LatticeVal evalFP2SI(SCCPPass& pass, FP2SIInst& inst) const;
        SCCPPass::LatticeVal evalSI2FP(SCCPPass& pass, SI2FPInst& inst) const;
        SCCPPass::LatticeVal evalZext(SCCPPass& pass, ZextInst& inst) const;
        SCCPPass::LatticeVal evalPhi(SCCPPass& pass, PhiInst& inst, Block* block) const;
    };

    // SCCP 常量替换：将可确定寄存器替换为立即数
    class SCCPReplaceVisitor : public InsVisitor_t<void, SCCPPass&>
    {
      public:
        void visit(LoadInst& inst, SCCPPass& pass) override;
        void visit(StoreInst& inst, SCCPPass& pass) override;
        void visit(ArithmeticInst& inst, SCCPPass& pass) override;
        void visit(IcmpInst& inst, SCCPPass& pass) override;
        void visit(FcmpInst& inst, SCCPPass& pass) override;
        void visit(AllocaInst& inst, SCCPPass& pass) override;
        void visit(BrCondInst& inst, SCCPPass& pass) override;
        void visit(BrUncondInst& inst, SCCPPass& pass) override;
        void visit(GlbVarDeclInst& inst, SCCPPass& pass) override;
        void visit(CallInst& inst, SCCPPass& pass) override;
        void visit(FuncDeclInst& inst, SCCPPass& pass) override;
        void visit(FuncDefInst& inst, SCCPPass& pass) override;
        void visit(RetInst& inst, SCCPPass& pass) override;
        void visit(GEPInst& inst, SCCPPass& pass) override;
        void visit(FP2SIInst& inst, SCCPPass& pass) override;
        void visit(SI2FPInst& inst, SCCPPass& pass) override;
        void visit(ZextInst& inst, SCCPPass& pass) override;
        void visit(PhiInst& inst, SCCPPass& pass) override;

      private:
        void replaceOperandIfConst(SCCPPass& pass, Operand*& op);
    };

}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_SCCP_VISITOR_H__
