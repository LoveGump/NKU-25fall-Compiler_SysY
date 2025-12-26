#ifndef __MIDDLEEND_VISITOR_UTILS_RENAME_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_RENAME_VISITOR_H__

#include <middleend/ir_visitor.h>
#include <middleend/module/ir_instruction.h>
#include <map>

namespace ME
{
    using RegRename_t = InsVisitor_t<void, RegMap&>;

    class RegRename : public RegRename_t
    {
      public:
        RegRename() = default;

        void visit(LoadInst&, RegMap&) override;
        void visit(StoreInst&, RegMap&) override;
        void visit(ArithmeticInst&, RegMap&) override;
        void visit(IcmpInst&, RegMap&) override;
        void visit(FcmpInst&, RegMap&) override;
        void visit(AllocaInst&, RegMap&) override;
        void visit(BrCondInst&, RegMap&) override;
        void visit(BrUncondInst&, RegMap&) override;
        void visit(GlbVarDeclInst&, RegMap&) override;
        void visit(CallInst&, RegMap&) override;
        void visit(FuncDeclInst&, RegMap&) override;
        void visit(FuncDefInst&, RegMap&) override;
        void visit(RetInst&, RegMap&) override;
        void visit(GEPInst&, RegMap&) override;
        void visit(FP2SIInst&, RegMap&) override;
        void visit(SI2FPInst&, RegMap&) override;
        void visit(ZextInst&, RegMap&) override;
        void visit(PhiInst&, RegMap&) override;
    };

    class SrcRegRename : public RegRename_t
    {
      public:
        SrcRegRename() = default;

        void visit(LoadInst&, RegMap&) override;
        void visit(StoreInst&, RegMap&) override;
        void visit(ArithmeticInst&, RegMap&) override;
        void visit(IcmpInst&, RegMap&) override;
        void visit(FcmpInst&, RegMap&) override;
        void visit(AllocaInst&, RegMap&) override;
        void visit(BrCondInst&, RegMap&) override;
        void visit(BrUncondInst&, RegMap&) override;
        void visit(GlbVarDeclInst&, RegMap&) override;
        void visit(CallInst&, RegMap&) override;
        void visit(FuncDeclInst&, RegMap&) override;
        void visit(FuncDefInst&, RegMap&) override;
        void visit(RetInst&, RegMap&) override;
        void visit(GEPInst&, RegMap&) override;
        void visit(FP2SIInst&, RegMap&) override;
        void visit(SI2FPInst&, RegMap&) override;
        void visit(ZextInst&, RegMap&) override;
        void visit(PhiInst&, RegMap&) override;
    };

    class ResRegRename : public RegRename_t
    {
      public:
        ResRegRename() = default;

        void visit(LoadInst&, RegMap&) override;
        void visit(StoreInst&, RegMap&) override;
        void visit(ArithmeticInst&, RegMap&) override;
        void visit(IcmpInst&, RegMap&) override;
        void visit(FcmpInst&, RegMap&) override;
        void visit(AllocaInst&, RegMap&) override;
        void visit(BrCondInst&, RegMap&) override;
        void visit(BrUncondInst&, RegMap&) override;
        void visit(GlbVarDeclInst&, RegMap&) override;
        void visit(CallInst&, RegMap&) override;
        void visit(FuncDeclInst&, RegMap&) override;
        void visit(FuncDefInst&, RegMap&) override;
        void visit(RetInst&, RegMap&) override;
        void visit(GEPInst&, RegMap&) override;
        void visit(FP2SIInst&, RegMap&) override;
        void visit(SI2FPInst&, RegMap&) override;
        void visit(ZextInst&, RegMap&) override;
        void visit(PhiInst&, RegMap&) override;
    };

    using OperandMap = std::map<size_t, Operand*>;  // 寄存器号 -> 重命名后操作数

    using OperandRename_t = InsVisitor_t<void, OperandMap&>;

    // 对单个操作数进行重命名
    void renameOperand(Operand*& operand, OperandMap& renameMap);

    // 操作数重命名，将指令中的操作数根据映射表进行替换
    class OperandRename : public OperandRename_t
    {
      public:
        OperandRename() = default;

        void visit(LoadInst&, OperandMap&) override;
        void visit(StoreInst&, OperandMap&) override;
        void visit(ArithmeticInst&, OperandMap&) override;
        void visit(IcmpInst&, OperandMap&) override;
        void visit(FcmpInst&, OperandMap&) override;
        void visit(AllocaInst&, OperandMap&) override;
        void visit(BrCondInst&, OperandMap&) override;
        void visit(BrUncondInst&, OperandMap&) override;
        void visit(GlbVarDeclInst&, OperandMap&) override;
        void visit(CallInst&, OperandMap&) override;
        void visit(FuncDeclInst&, OperandMap&) override;
        void visit(FuncDefInst&, OperandMap&) override;
        void visit(RetInst&, OperandMap&) override;
        void visit(GEPInst&, OperandMap&) override;
        void visit(FP2SIInst&, OperandMap&) override;
        void visit(SI2FPInst&, OperandMap&) override;
        void visit(ZextInst&, OperandMap&) override;
        void visit(PhiInst&, OperandMap&) override;
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_RENAME_VISITOR_H__
