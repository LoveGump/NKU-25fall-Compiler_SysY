#include <middleend/visitor/utils/clone_visitor.h>

namespace ME
{
    Instruction* InstCloner::visit(LoadInst& inst) { return new LoadInst(inst.dt, inst.ptr, inst.res, inst.comment); }

    Instruction* InstCloner::visit(StoreInst& inst)
    {
        return new StoreInst(inst.dt, inst.val, inst.ptr, inst.comment);
    }

    Instruction* InstCloner::visit(ArithmeticInst& inst)
    {
        return new ArithmeticInst(inst.opcode, inst.dt, inst.lhs, inst.rhs, inst.res, inst.comment);
    }

    Instruction* InstCloner::visit(IcmpInst& inst) { return new IcmpInst(inst.dt, inst.cond, inst.lhs, inst.rhs, inst.res); }

    Instruction* InstCloner::visit(FcmpInst& inst) { return new FcmpInst(inst.dt, inst.cond, inst.lhs, inst.rhs, inst.res); }

    Instruction* InstCloner::visit(AllocaInst& inst) { return new AllocaInst(inst.dt, inst.res, inst.dims, inst.comment); }

    Instruction* InstCloner::visit(BrCondInst& inst)
    {
        return new BrCondInst(inst.cond, inst.trueTar, inst.falseTar, inst.comment);
    }

    Instruction* InstCloner::visit(BrUncondInst& inst) { return new BrUncondInst(inst.target, inst.comment); }

    Instruction* InstCloner::visit(GlbVarDeclInst& inst)
    {
        if (inst.init) return new GlbVarDeclInst(inst.dt, inst.name, inst.init);
        return new GlbVarDeclInst(inst.dt, inst.name, inst.initList);
    }

    Instruction* InstCloner::visit(CallInst& inst) { return new CallInst(inst.retType, inst.funcName, inst.args, inst.res, inst.comment); }

    Instruction* InstCloner::visit(FuncDeclInst& inst)
    {
        return new FuncDeclInst(inst.retType, inst.funcName, inst.argTypes, inst.isVarArg, inst.comment);
    }

    Instruction* InstCloner::visit(FuncDefInst& inst)
    {
        return new FuncDefInst(inst.retType, inst.funcName, inst.argRegs, inst.comment);
    }

    Instruction* InstCloner::visit(RetInst& inst) { return new RetInst(inst.rt, inst.res, inst.comment); }

    Instruction* InstCloner::visit(GEPInst& inst)
    {
        return new GEPInst(inst.dt, inst.idxType, inst.basePtr, inst.res, inst.dims, inst.idxs);
    }

    Instruction* InstCloner::visit(FP2SIInst& inst) { return new FP2SIInst(inst.src, inst.dest); }

    Instruction* InstCloner::visit(SI2FPInst& inst) { return new SI2FPInst(inst.src, inst.dest); }

    Instruction* InstCloner::visit(ZextInst& inst) { return new ZextInst(inst.from, inst.to, inst.src, inst.dest); }

    Instruction* InstCloner::visit(PhiInst& inst)
    {
        auto* phi = new PhiInst(inst.dt, inst.res, inst.comment);
        phi->incomingVals = inst.incomingVals;
        return phi;
    }
}  // namespace ME
