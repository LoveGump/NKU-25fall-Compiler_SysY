#ifndef __MIDDLEEND_VISITOR_UTILS_EXPR_KEY_VISITOR_H__
#define __MIDDLEEND_VISITOR_UTILS_EXPR_KEY_VISITOR_H__

#include <interfaces/middleend/ir_visitor.h>
#include <interfaces/middleend/ir_defs.h>
#include <string>

namespace ME
{
    class Operand;

    // 表达式键生成访问者，可以获得指令对应的唯一表达式键字符串
    class ExprKeyVisitor : public InsVisitor_t<void>
    {
      public:
        std::string result;

        void visit(ArithmeticInst& inst) override;
        void visit(IcmpInst& inst) override;
        void visit(FcmpInst& inst) override;
        void visit(GEPInst& inst) override;
        void visit(ZextInst& inst) override;
        void visit(SI2FPInst& inst) override;
        void visit(FP2SIInst& inst) override;

        // 非 CSE 候选指令返回空
        void visit(LoadInst&) override {}
        void visit(StoreInst&) override {}
        void visit(AllocaInst&) override {}
        void visit(BrCondInst&) override {}
        void visit(BrUncondInst&) override {}
        void visit(GlbVarDeclInst&) override {}
        void visit(CallInst&) override {}
        void visit(FuncDeclInst&) override {}
        void visit(FuncDefInst&) override {}
        void visit(RetInst&) override {}
        void visit(PhiInst&) override {}

      private:
        static std::string operandKey(Operand* op);
        static bool        isCommutativeOp(Operator op);
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_UTILS_EXPR_KEY_VISITOR_H__
