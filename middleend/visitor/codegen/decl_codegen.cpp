#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>

namespace ME
{
    void ASTCodeGen::visit(FE::AST::Initializer& node, Module* m)
    {
        (void)m;
        ERROR("Initializer should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::InitializerList& node, Module* m)
    {
        (void)m;
        ERROR("InitializerList should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::VarDeclarator& node, Module* m)
    {
        (void)m;
        ERROR("VarDeclarator should not appear here, at line %d", node.line_num);
    }
    void ASTCodeGen::visit(FE::AST::ParamDeclarator& node, Module* m)
    {
        (void)m;
        ERROR("ParamDeclarator should not appear here, at line %d", node.line_num);
    }

    void ASTCodeGen::visit(FE::AST::VarDeclaration& node, Module* m)
    {
        (void)m;
        if (!curFunc || !node.decls) return;

        DataType elemType = convert(node.type);

        for (auto* vd : *(node.decls))
        {
            if (!vd) continue;
            auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(vd->lval);
            if (!lval || !lval->entry) continue;

            std::vector<int> dims = collectArrayDims(lval->indices);

            size_t      allocaReg  = getNewRegId();
            AllocaInst* allocaInst = nullptr;
            if (dims.empty())
                allocaInst = createAllocaInst(elemType, allocaReg);
            else
                allocaInst = createAllocaInst(elemType, allocaReg, dims);
            insertAllocaInst(allocaInst);

            name2reg.addSymbol(lval->entry, allocaReg);
            FE::AST::VarAttr attr(node.type, node.isConstDecl, scopeDepth);
            attr.arrayDims      = dims;
            reg2attr[allocaReg] = attr;

            Operand* ptr = getRegOperand(allocaReg);

            if (vd->init)
            {
                if (dims.empty())
                {
                    if (auto* init = dynamic_cast<FE::AST::Initializer*>(vd->init))
                    {
                        if (init->init_val)
                        {
                            apply(*this, *init->init_val, m);
                            size_t   initReg  = getMaxReg();
                            DataType initType = convert(init->init_val->attr.val.value.type);
                            initReg           = ensureType(initReg, initType, elemType);
                            insert(createStoreInst(elemType, initReg, ptr));
                        }
                    }
                }
                else
                {
                    std::vector<int> arrayDims;
                    arrayDims.reserve(dims.size());
                    for (int dim : dims)
                    {
                        if (dim > 0) arrayDims.push_back(dim);
                    }

                    if (!arrayDims.empty())
                    {
                        size_t totalElems = 1;
                        for (int dim : arrayDims) totalElems *= static_cast<size_t>(dim);
                        if (totalElems == 0) totalElems = 1;

                        std::vector<FE::AST::VarValue> initValues(totalElems, makeZeroValue(node.type));
                        fillGlobalArrayInit(vd->init, node.type, arrayDims, initValues);

                        std::vector<size_t> strides(arrayDims.size(), 1);
                        for (int idx = static_cast<int>(arrayDims.size()) - 2; idx >= 0; --idx)
                        {
                            strides[idx] = strides[idx + 1] * static_cast<size_t>(arrayDims[idx + 1]);
                        }

                        std::vector<Operand*> idxOps;
                        idxOps.reserve(arrayDims.size() + 1);
                        idxOps.push_back(getImmeI32Operand(0));

                        auto emitStores = [&](auto&& self, size_t dimIdx, size_t offset) -> void {
                            if (dimIdx == arrayDims.size())
                            {
                                if (offset >= initValues.size()) return;
                                size_t   gepReg = getNewRegId();
                                GEPInst* gep    = createGEP_I32Inst(elemType, ptr, arrayDims, idxOps, gepReg);
                                insert(gep);

                                Operand* valOp = nullptr;
                                if (elemType == DataType::F32)
                                    valOp = getImmeF32Operand(initValues[offset].getFloat());
                                else
                                    valOp = getImmeI32Operand(initValues[offset].getInt());
                                insert(createStoreInst(elemType, valOp, getRegOperand(gepReg)));
                                return;
                            }

                            size_t dimSize   = static_cast<size_t>(arrayDims[dimIdx]);
                            size_t blockSize = strides[dimIdx];
                            for (size_t idx = 0; idx < dimSize; ++idx)
                            {
                                idxOps.push_back(getImmeI32Operand(static_cast<int>(idx)));
                                self(self, dimIdx + 1, offset + idx * blockSize);
                                idxOps.pop_back();
                            }
                        };

                        emitStores(emitStores, 0, 0);

                        emitRuntimeArrayInit(vd->init, ptr, elemType, arrayDims, m);
                    }
                }
            }
            else if (dims.empty())
            {
                Operand* zero = (elemType == DataType::F32) ? static_cast<Operand*>(getImmeF32Operand(0.0f))
                                                            : static_cast<Operand*>(getImmeI32Operand(0));
                insert(createStoreInst(elemType, zero, ptr));
            }
        }
    }
}  // namespace ME
