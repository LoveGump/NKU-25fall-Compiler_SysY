#ifndef __MIDDLEEND_VISITOR_UTILS_LICM_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_LICM_VISITOR_H__

#include <interfaces/middleend/ir_visitor.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>

namespace ME
{
    // 判断指令是否为可外提的标量运算
    class LICMScalarVisitor : public InsVisitor_t<bool>
    {
      public:
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
    };

    // 检测内存/调用相关指令（当前仅做纯标量外提）
    class LICMMemoryLikeVisitor : public InsVisitor_t<bool>
    {
      public:
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
    };

    // 判断指令是否可以安全提前执行（用于条件块外提）
    class LICMSafeSpecVisitor : public InsVisitor_t<bool>
    {
      public:
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
    };

    // 检测全局变量 load
    class LICMGlobalLoadVisitor : public InsVisitor_t<Operand*>
    {
      public:
        Operand* visit(LoadInst& inst) override;
        Operand* visit(StoreInst& inst) override;
        Operand* visit(ArithmeticInst& inst) override;
        Operand* visit(IcmpInst& inst) override;
        Operand* visit(FcmpInst& inst) override;
        Operand* visit(AllocaInst& inst) override;
        Operand* visit(BrCondInst& inst) override;
        Operand* visit(BrUncondInst& inst) override;
        Operand* visit(GlbVarDeclInst& inst) override;
        Operand* visit(CallInst& inst) override;
        Operand* visit(FuncDeclInst& inst) override;
        Operand* visit(FuncDefInst& inst) override;
        Operand* visit(RetInst& inst) override;
        Operand* visit(GEPInst& inst) override;
        Operand* visit(FP2SIInst& inst) override;
        Operand* visit(SI2FPInst& inst) override;
        Operand* visit(ZextInst& inst) override;
        Operand* visit(PhiInst& inst) override;
    };

    // 检测写入全局变量的 store
    class LICMGlobalStoreVisitor : public InsVisitor_t<Operand*>
    {
      public:
        Operand* visit(LoadInst& inst) override;
        Operand* visit(StoreInst& inst) override;
        Operand* visit(ArithmeticInst& inst) override;
        Operand* visit(IcmpInst& inst) override;
        Operand* visit(FcmpInst& inst) override;
        Operand* visit(AllocaInst& inst) override;
        Operand* visit(BrCondInst& inst) override;
        Operand* visit(BrUncondInst& inst) override;
        Operand* visit(GlbVarDeclInst& inst) override;
        Operand* visit(CallInst& inst) override;
        Operand* visit(FuncDeclInst& inst) override;
        Operand* visit(FuncDefInst& inst) override;
        Operand* visit(RetInst& inst) override;
        Operand* visit(GEPInst& inst) override;
        Operand* visit(FP2SIInst& inst) override;
        Operand* visit(SI2FPInst& inst) override;
        Operand* visit(ZextInst& inst) override;
        Operand* visit(PhiInst& inst) override;
    };

    // 检测调用指令
    class LICMCallVisitor : public InsVisitor_t<bool>
    {
      public:
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
    };

    // 检测递归调用
    class LICMSelfCallVisitor : public InsVisitor_t<bool, const std::string&>
    {
      public:
        bool visit(LoadInst& inst, const std::string& name) override;
        bool visit(StoreInst& inst, const std::string& name) override;
        bool visit(ArithmeticInst& inst, const std::string& name) override;
        bool visit(IcmpInst& inst, const std::string& name) override;
        bool visit(FcmpInst& inst, const std::string& name) override;
        bool visit(AllocaInst& inst, const std::string& name) override;
        bool visit(BrCondInst& inst, const std::string& name) override;
        bool visit(BrUncondInst& inst, const std::string& name) override;
        bool visit(GlbVarDeclInst& inst, const std::string& name) override;
        bool visit(CallInst& inst, const std::string& name) override;
        bool visit(FuncDeclInst& inst, const std::string& name) override;
        bool visit(FuncDefInst& inst, const std::string& name) override;
        bool visit(RetInst& inst, const std::string& name) override;
        bool visit(GEPInst& inst, const std::string& name) override;
        bool visit(FP2SIInst& inst, const std::string& name) override;
        bool visit(SI2FPInst& inst, const std::string& name) override;
        bool visit(ZextInst& inst, const std::string& name) override;
        bool visit(PhiInst& inst, const std::string& name) override;
    };

    // 更新分支目标的访问者
    class LICMBranchReplaceVisitor : public InsVisitor_t<void, Operand*, Operand*>
    {
      public:
        void visit(LoadInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(StoreInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(ArithmeticInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(IcmpInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FcmpInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(AllocaInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(BrCondInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(BrUncondInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(GlbVarDeclInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(CallInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FuncDeclInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FuncDefInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(RetInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(GEPInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FP2SIInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(SI2FPInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(ZextInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(PhiInst& inst, Operand* oldLabel, Operand* newLabel) override;
    };

    // 更新 phi 的来边标签
    class LICMPhiReplaceVisitor : public InsVisitor_t<void, Operand*, Operand*>
    {
      public:
        void visit(LoadInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(StoreInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(ArithmeticInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(IcmpInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FcmpInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(AllocaInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(BrCondInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(BrUncondInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(GlbVarDeclInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(CallInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FuncDeclInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FuncDefInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(RetInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(GEPInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(FP2SIInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(SI2FPInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(ZextInst& inst, Operand* oldLabel, Operand* newLabel) override;
        void visit(PhiInst& inst, Operand* oldLabel, Operand* newLabel) override;
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_LICM_VISITOR_H__
