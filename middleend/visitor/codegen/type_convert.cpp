#include <middleend/visitor/codegen/ast_codegen.h>

namespace ME
{
    namespace
    {
        std::array<DataType, FE::AST::maxTypeIdx + 1> at2dt = []() {
            std::array<DataType, FE::AST::maxTypeIdx + 1> ret{};
            ret.fill(DataType::UNK);

            ret[static_cast<size_t>(FE::AST::Type_t::UNK)]   = DataType::UNK;
            ret[static_cast<size_t>(FE::AST::Type_t::VOID)]  = DataType::VOID;
            ret[static_cast<size_t>(FE::AST::Type_t::BOOL)]  = DataType::I1;
            ret[static_cast<size_t>(FE::AST::Type_t::INT)]   = DataType::I32;
            ret[static_cast<size_t>(FE::AST::Type_t::LL)]    = DataType::I32;  // treat as I32
            ret[static_cast<size_t>(FE::AST::Type_t::FLOAT)] = DataType::F32;

            return ret;
        }();
    }  // namespace

    // 将 FE::AST 类型 转换为中间表示的 类型
    DataType ASTCodeGen::convert(FE::AST::Type* at)
    {
        if (!at) return DataType::UNK;
        if (at->getTypeGroup() == FE::AST::TypeGroup::POINTER) return DataType::PTR;
        return at2dt[static_cast<size_t>(at->getBaseType())];
    }

    struct UnaryOperators
    {
        static void addInt(ASTCodeGen* codegen, Block* block, size_t srcReg)
        {
            (void)codegen;
            (void)block;
            (void)srcReg;
        }
        static void subInt(ASTCodeGen* codegen, Block* block, size_t srcReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* neg  = codegen->createArithmeticI32Inst_ImmeLeft(Operator::SUB, 0, srcReg, dest);
            block->insert(neg);
        }
        static void notInt(ASTCodeGen* codegen, Block* block, size_t srcReg)
        {
            size_t    dest    = codegen->getNewRegId();
            IcmpInst* notInst = codegen->createIcmpInst_ImmeRight(ICmpOp::EQ, srcReg, 0, dest);
            block->insert(notInst);
        }

        static void addFloat(ASTCodeGen* codegen, Block* block, size_t srcReg)
        {
            (void)codegen;
            (void)block;
            (void)srcReg;
        }
        static void subFloat(ASTCodeGen* codegen, Block* block, size_t srcReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* neg  = codegen->createArithmeticF32Inst_ImmeLeft(Operator::FSUB, 0.0f, srcReg, dest);
            block->insert(neg);
        }
        static void notFloat(ASTCodeGen* codegen, Block* block, size_t srcReg)
        {
            size_t    dest    = codegen->getNewRegId();
            FcmpInst* notInst = codegen->createFcmpInst_ImmeRight(FCmpOp::OEQ, srcReg, 0.0f, dest);
            block->insert(notInst);
        }
    };

    struct BinaryOperators
    {
        static void addInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* add  = codegen->createArithmeticI32Inst(Operator::ADD, lhsReg, rhsReg, dest);
            block->insert(add);
        }
        static void subInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* sub  = codegen->createArithmeticI32Inst(Operator::SUB, lhsReg, rhsReg, dest);
            block->insert(sub);
        }
        static void mulInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* mul  = codegen->createArithmeticI32Inst(Operator::MUL, lhsReg, rhsReg, dest);
            block->insert(mul);
        }
        static void divInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* div  = codegen->createArithmeticI32Inst(Operator::DIV, lhsReg, rhsReg, dest);
            block->insert(div);
        }
        static void modInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* mod  = codegen->createArithmeticI32Inst(Operator::MOD, lhsReg, rhsReg, dest);
            block->insert(mod);
        }
        static void gtInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            IcmpInst* gt   = codegen->createIcmpInst(ICmpOp::SGT, lhsReg, rhsReg, dest);
            block->insert(gt);
        }
        static void geInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            IcmpInst* ge   = codegen->createIcmpInst(ICmpOp::SGE, lhsReg, rhsReg, dest);
            block->insert(ge);
        }
        static void ltInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            IcmpInst* lt   = codegen->createIcmpInst(ICmpOp::SLT, lhsReg, rhsReg, dest);
            block->insert(lt);
        }
        static void leInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            IcmpInst* le   = codegen->createIcmpInst(ICmpOp::SLE, lhsReg, rhsReg, dest);
            block->insert(le);
        }
        static void eqInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            IcmpInst* eq   = codegen->createIcmpInst(ICmpOp::EQ, lhsReg, rhsReg, dest);
            block->insert(eq);
        }
        static void neqInt(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            IcmpInst* neq  = codegen->createIcmpInst(ICmpOp::NE, lhsReg, rhsReg, dest);
            block->insert(neq);
        }

        static void addFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* add  = codegen->createArithmeticF32Inst(Operator::FADD, lhsReg, rhsReg, dest);
            block->insert(add);
        }
        static void subFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* sub  = codegen->createArithmeticF32Inst(Operator::FSUB, lhsReg, rhsReg, dest);
            block->insert(sub);
        }
        static void mulFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* mul  = codegen->createArithmeticF32Inst(Operator::FMUL, lhsReg, rhsReg, dest);
            block->insert(mul);
        }
        static void divFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t          dest = codegen->getNewRegId();
            ArithmeticInst* div  = codegen->createArithmeticF32Inst(Operator::FDIV, lhsReg, rhsReg, dest);
            block->insert(div);
        }
        static void modFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            (void)codegen;
            (void)block;
            (void)lhsReg;
            (void)rhsReg;
            ERROR("Float modulo not supported");
        }
        static void gtFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            FcmpInst* gt   = codegen->createFcmpInst(FCmpOp::OGT, lhsReg, rhsReg, dest);
            block->insert(gt);
        }
        static void geFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            FcmpInst* ge   = codegen->createFcmpInst(FCmpOp::OGE, lhsReg, rhsReg, dest);
            block->insert(ge);
        }
        static void ltFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            FcmpInst* lt   = codegen->createFcmpInst(FCmpOp::OLT, lhsReg, rhsReg, dest);
            block->insert(lt);
        }
        static void leFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            FcmpInst* le   = codegen->createFcmpInst(FCmpOp::OLE, lhsReg, rhsReg, dest);
            block->insert(le);
        }
        static void eqFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            FcmpInst* eq   = codegen->createFcmpInst(FCmpOp::OEQ, lhsReg, rhsReg, dest);
            block->insert(eq);
        }
        static void neqFloat(ASTCodeGen* codegen, Block* block, size_t lhsReg, size_t rhsReg)
        {
            size_t    dest = codegen->getNewRegId();
            FcmpInst* neq  = codegen->createFcmpInst(FCmpOp::ONE, lhsReg, rhsReg, dest);
            block->insert(neq);
        }
    };

    //
    void ASTCodeGen::handleUnaryCalc(FE::AST::ExprNode& node, FE::AST::Operator uop, Block* block, Module* m)
    {
        using UnaryOpFunc                                           = void (*)(ASTCodeGen*, Block*, size_t);
        static std::map<FE::AST::Operator, UnaryOpFunc> unaryIntOps = {
            {FE::AST::Operator::ADD, UnaryOperators::addInt},
            {FE::AST::Operator::SUB, UnaryOperators::subInt},
            {FE::AST::Operator::NOT, UnaryOperators::notInt},
        };
        static std::map<FE::AST::Operator, UnaryOpFunc> unaryFloatOps = {
            {FE::AST::Operator::ADD, UnaryOperators::addFloat},
            {FE::AST::Operator::SUB, UnaryOperators::subFloat},
            {FE::AST::Operator::NOT, UnaryOperators::notFloat},
        };

        apply(*this, node, m);
        size_t srcReg = getMaxReg();

        DataType srcType = convert(node.attr.val.value.type);

        if (srcType == DataType::I1)
        {
            auto convInsts = createTypeConvertInst(srcType, DataType::I32, srcReg);
            for (auto& inst : convInsts) block->insert(inst);
            srcReg  = getMaxReg();
            srcType = DataType::I32;
        }

        ASSERT(srcType == DataType::I32 || srcType == DataType::F32);

        UnaryOpFunc opFunc = nullptr;
        if (srcType == DataType::I32)
            opFunc = unaryIntOps[uop];
        else if (srcType == DataType::F32)
            opFunc = unaryFloatOps[uop];
        else
            ERROR("Unary op type not supported");

        if (!opFunc) ERROR("Unary op not supported");

        opFunc(this, block, srcReg);
    }

    DataType promoteType(DataType t1, DataType t2)
    {
        if (t1 == DataType::F32 || t2 == DataType::F32) return DataType::F32;
        if (t1 == DataType::I32 || t2 == DataType::I32) return DataType::I32;
        return DataType::I1;
    }

    void ASTCodeGen::handleBinaryCalc(
        FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, FE::AST::Operator bop, Block* block, Module* m)
    {
        using BinaryOpFunc                                            = void (*)(ASTCodeGen*, Block*, size_t, size_t);
        static std::map<FE::AST::Operator, BinaryOpFunc> binaryIntOps = {
            {FE::AST::Operator::ADD, BinaryOperators::addInt},
            {FE::AST::Operator::SUB, BinaryOperators::subInt},
            {FE::AST::Operator::MUL, BinaryOperators::mulInt},
            {FE::AST::Operator::DIV, BinaryOperators::divInt},
            {FE::AST::Operator::MOD, BinaryOperators::modInt},
            {FE::AST::Operator::GT, BinaryOperators::gtInt},
            {FE::AST::Operator::GE, BinaryOperators::geInt},
            {FE::AST::Operator::LT, BinaryOperators::ltInt},
            {FE::AST::Operator::LE, BinaryOperators::leInt},
            {FE::AST::Operator::EQ, BinaryOperators::eqInt},
            {FE::AST::Operator::NEQ, BinaryOperators::neqInt},
        };
        static std::map<FE::AST::Operator, BinaryOpFunc> binaryFloatOps = {
            {FE::AST::Operator::ADD, BinaryOperators::addFloat},
            {FE::AST::Operator::SUB, BinaryOperators::subFloat},
            {FE::AST::Operator::MUL, BinaryOperators::mulFloat},
            {FE::AST::Operator::DIV, BinaryOperators::divFloat},
            {FE::AST::Operator::MOD, BinaryOperators::modFloat},
            {FE::AST::Operator::GT, BinaryOperators::gtFloat},
            {FE::AST::Operator::GE, BinaryOperators::geFloat},
            {FE::AST::Operator::LT, BinaryOperators::ltFloat},
            {FE::AST::Operator::LE, BinaryOperators::leFloat},
            {FE::AST::Operator::EQ, BinaryOperators::eqFloat},
            {FE::AST::Operator::NEQ, BinaryOperators::neqFloat},
        };

        apply(*this, lhs, m);
        size_t lhsReg = getMaxReg();
        apply(*this, rhs, m);
        size_t rhsReg = getMaxReg();

        DataType lhsType = convert(lhs.attr.val.value.type);
        DataType rhsType = convert(rhs.attr.val.value.type);
        ASSERT(lhsType == DataType::I1 || lhsType == DataType::I32 || lhsType == DataType::F32);
        ASSERT(rhsType == DataType::I1 || rhsType == DataType::I32 || rhsType == DataType::F32);
        DataType pType = promoteType(lhsType, rhsType);

        if (pType == DataType::I1)
        {
            auto lhsConvInsts = createTypeConvertInst(pType, DataType::I32, lhsReg);
            for (auto& inst : lhsConvInsts) block->insert(inst);
            lhsReg = getMaxReg();

            auto rhsConvInsts = createTypeConvertInst(pType, DataType::I32, rhsReg);
            for (auto& inst : rhsConvInsts) block->insert(inst);
            rhsReg = getMaxReg();

            pType = DataType::I32;
        }
        else if (lhsType != pType)
        {
            auto convInsts = createTypeConvertInst(lhsType, pType, lhsReg);
            for (auto& inst : convInsts) block->insert(inst);
            lhsReg = getMaxReg();
        }
        else if (rhsType != pType)
        {
            auto convInsts = createTypeConvertInst(rhsType, pType, rhsReg);
            for (auto& inst : convInsts) block->insert(inst);
            rhsReg = getMaxReg();
        }

        BinaryOpFunc opFunc = nullptr;
        if (pType == DataType::I32)
            opFunc = binaryIntOps[bop];
        else if (pType == DataType::F32)
            opFunc = binaryFloatOps[bop];
        else
            ERROR("Binary op type not supported");

        if (!opFunc) ERROR("Binary op not supported");
        opFunc(this, block, lhsReg, rhsReg);
    }
}  // namespace ME
