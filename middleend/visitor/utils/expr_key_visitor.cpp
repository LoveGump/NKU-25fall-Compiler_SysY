#include <middleend/visitor/utils/expr_key_visitor.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <sstream>
#include <algorithm>

namespace ME
{
    std::string ExprKeyVisitor::operandKey(Operand* op)
    {
        return op ? op->toString() : "null";
    }

    bool ExprKeyVisitor::isCommutativeOp(Operator op)
    {
        switch (op)
        {
            case Operator::ADD:
            case Operator::MUL:
            case Operator::FADD:
            case Operator::FMUL:
            case Operator::BITAND:
            case Operator::BITXOR: return true;
            default: return false;
        }
    }

    void ExprKeyVisitor::visit(ArithmeticInst& inst)
    {
        std::string lhsKey = operandKey(inst.lhs);
        std::string rhsKey = operandKey(inst.rhs);
        if (isCommutativeOp(inst.opcode) && lhsKey > rhsKey) std::swap(lhsKey, rhsKey);

        std::stringstream ss;
        ss << static_cast<int>(inst.opcode) << "|dt:" << static_cast<int>(inst.dt)
           << "|lhs:" << lhsKey << "|rhs:" << rhsKey;
        result = ss.str();
    }

    void ExprKeyVisitor::visit(IcmpInst& inst)
    {
        std::string lhsKey = operandKey(inst.lhs);
        std::string rhsKey = operandKey(inst.rhs);
        ICmpOp      cond   = inst.cond;

        if (lhsKey > rhsKey)
        {
            std::swap(lhsKey, rhsKey);
            switch (cond)
            {
                case ICmpOp::SGT: cond = ICmpOp::SLT; break;
                case ICmpOp::SLT: cond = ICmpOp::SGT; break;
                case ICmpOp::SGE: cond = ICmpOp::SLE; break;
                case ICmpOp::SLE: cond = ICmpOp::SGE; break;
                case ICmpOp::UGT: cond = ICmpOp::ULT; break;
                case ICmpOp::ULT: cond = ICmpOp::UGT; break;
                case ICmpOp::UGE: cond = ICmpOp::ULE; break;
                case ICmpOp::ULE: cond = ICmpOp::UGE; break;
                default: break;
            }
        }

        std::stringstream ss;
        ss << static_cast<int>(inst.opcode) << "|dt:" << static_cast<int>(inst.dt)
           << "|cond:" << static_cast<int>(cond) << "|lhs:" << lhsKey << "|rhs:" << rhsKey;
        result = ss.str();
    }

    void ExprKeyVisitor::visit(FcmpInst& inst)
    {
        std::string lhsKey = operandKey(inst.lhs);
        std::string rhsKey = operandKey(inst.rhs);
        FCmpOp      cond   = inst.cond;

        if (lhsKey > rhsKey)
        {
            std::swap(lhsKey, rhsKey);
            switch (cond)
            {
                case FCmpOp::OGT: cond = FCmpOp::OLT; break;
                case FCmpOp::OLT: cond = FCmpOp::OGT; break;
                case FCmpOp::OGE: cond = FCmpOp::OLE; break;
                case FCmpOp::OLE: cond = FCmpOp::OGE; break;
                case FCmpOp::UGT: cond = FCmpOp::ULT; break;
                case FCmpOp::ULT: cond = FCmpOp::UGT; break;
                case FCmpOp::UGE: cond = FCmpOp::ULE; break;
                case FCmpOp::ULE: cond = FCmpOp::UGE; break;
                default: break;
            }
        }

        std::stringstream ss;
        ss << static_cast<int>(inst.opcode) << "|dt:" << static_cast<int>(inst.dt)
           << "|cond:" << static_cast<int>(cond) << "|lhs:" << lhsKey << "|rhs:" << rhsKey;
        result = ss.str();
    }

    void ExprKeyVisitor::visit(GEPInst& inst)
    {
        std::stringstream ss;
        ss << static_cast<int>(inst.opcode) << "|dt:" << static_cast<int>(inst.dt)
           << "|it:" << static_cast<int>(inst.idxType) << "|base:" << operandKey(inst.basePtr) << "|dims:";
        for (int dim : inst.dims) ss << dim << ",";
        ss << "|idx:";
        for (auto* idx : inst.idxs) ss << operandKey(idx) << ",";
        result = ss.str();
    }

    void ExprKeyVisitor::visit(ZextInst& inst)
    {
        std::stringstream ss;
        ss << static_cast<int>(inst.opcode) << "|from:" << static_cast<int>(inst.from)
           << "|to:" << static_cast<int>(inst.to) << "|src:" << operandKey(inst.src);
        result = ss.str();
    }

    void ExprKeyVisitor::visit(SI2FPInst& inst)
    {
        std::stringstream ss;
        ss << static_cast<int>(inst.opcode) << "|src:" << operandKey(inst.src);
        result = ss.str();
    }

    void ExprKeyVisitor::visit(FP2SIInst& inst)
    {
        std::stringstream ss;
        ss << static_cast<int>(inst.opcode) << "|src:" << operandKey(inst.src);
        result = ss.str();
    }
}  // namespace ME
