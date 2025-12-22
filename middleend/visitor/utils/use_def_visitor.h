#ifndef __MIDDLEEND_VISITOR_UTILS_USE_DEF_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_USE_DEF_VISITOR_H__

#include <interfaces/middleend/ir_visitor.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <set>
#include <map>

namespace ME
{
    // 收集使用的寄存器
    class UseCollector : public InsVisitor_t<void>
    {
      public:
        std::map<size_t, int>& useCounts;  // 寄存器编号 -> 使用次数

        UseCollector(std::map<size_t, int>& counts);

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
        void addUse(Operand* op);
    };

    // 收集定义的寄存器
    class DefCollector : public InsVisitor_t<void>
    {
      public:
        size_t defReg = 0;  // 定义的寄存器编号，若无定义则为0

        DefCollector() = default;
        size_t getResult() const { return defReg; }  // 获取定义的寄存器编号

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
        size_t getReg(Operand* op);
    };

    // 收集使用某寄存器的所有指令
    class UserCollector : public InsVisitor_t<void>
    {
      public:
        std::map<size_t, std::vector<Instruction*>> userMap;  // 寄存器编号 -> 使用该寄存器的指令列表
        Instruction*                                currentInst = nullptr;

        UserCollector() = default;

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
        void addUse(Operand* op);
    };
}  // namespace ME

#endif
