#ifndef __INTERFACES_MIDDLEEND_IR_VISITOR_H__
#define __INTERFACES_MIDDLEEND_IR_VISITOR_H__

#include <ivisitor.h>

namespace ME
{
    class Module;
    class Function;
    class Block;

    // Instructions
    class LoadInst;
    class StoreInst;
    class ArithmeticInst;
    class IcmpInst;
    class FcmpInst;
    class AllocaInst;
    class BrCondInst;
    class BrUncondInst;
    class GlbVarDeclInst;
    class CallInst;
    class FuncDeclInst;
    class FuncDefInst;
    class RetInst;
    class GEPInst;
    class FP2SIInst;
    class SI2FPInst;
    class ZextInst;
    class PhiInst;

    using NonInstTypeSet = TypeList<Module, Function, Block>;
    using InstTypeSet    = TypeList<LoadInst, StoreInst, ArithmeticInst, IcmpInst, FcmpInst, AllocaInst, BrCondInst,
           BrUncondInst, GlbVarDeclInst, CallInst, FuncDeclInst, FuncDefInst, RetInst, GEPInst, FP2SIInst, SI2FPInst,
           ZextInst, PhiInst>;
    using TypeSet        = type_list_utils::Concat_t<NonInstTypeSet, InstTypeSet>;

    template <typename... Ts>
    using Visitor_t = VisitSetFrom<TypeSet>::Visitor<Ts...>;
    using Visitor   = VisitSetFrom<TypeSet>::ErasedVisitor;

    template <typename... Ts>
    using InsVisitor_t = VisitSetFrom<InstTypeSet>::Visitor<Ts...>;
    using InsVisitor   = VisitSetFrom<InstTypeSet>::ErasedVisitor;

    class Visitable
    {
      public:
        virtual ~Visitable() = default;

      public:
        virtual void accept(Visitor& visitor) = 0;
    };

    class InsVisitable
    {
      public:
        virtual ~InsVisitable() = default;

      public:
        virtual void accept(InsVisitor& visitor) = 0;
    };
}  // namespace ME

#endif  // __INTERFACES_MIDDLEEND_IR_VISITOR_H__
