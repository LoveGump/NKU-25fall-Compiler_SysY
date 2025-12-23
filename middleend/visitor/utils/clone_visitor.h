#ifndef __MIDDLEEND_VISITOR_UTILS_CLONE_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_CLONE_VISITOR_H__

#include <interfaces/middleend/ir_visitor.h>
#include <middleend/module/ir_instruction.h>

namespace ME
{
    // 克隆 IR 指令的访问者
    class InstCloner : public InsVisitor_t<Instruction*>
    {
      public:
        Instruction* visit(LoadInst& inst) override;
        Instruction* visit(StoreInst& inst) override;
        Instruction* visit(ArithmeticInst& inst) override;
        Instruction* visit(IcmpInst& inst) override;
        Instruction* visit(FcmpInst& inst) override;
        Instruction* visit(AllocaInst& inst) override;
        Instruction* visit(BrCondInst& inst) override;
        Instruction* visit(BrUncondInst& inst) override;
        Instruction* visit(GlbVarDeclInst& inst) override;
        Instruction* visit(CallInst& inst) override;
        Instruction* visit(FuncDeclInst& inst) override;
        Instruction* visit(FuncDefInst& inst) override;
        Instruction* visit(RetInst& inst) override;
        Instruction* visit(GEPInst& inst) override;
        Instruction* visit(FP2SIInst& inst) override;
        Instruction* visit(SI2FPInst& inst) override;
        Instruction* visit(ZextInst& inst) override;
        Instruction* visit(PhiInst& inst) override;
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_CLONE_VISITOR_H__
