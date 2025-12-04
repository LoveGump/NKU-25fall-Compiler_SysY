#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>
#include <algorithm>

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

    size_t ASTCodeGen::fillArrayChunk(FE::AST::InitDecl* init, const std::vector<int>& dims, size_t dimIdx,
        size_t baseOffset, size_t chunkSize, std::vector<std::pair<size_t, FE::AST::Initializer*>>& slots)
    {
        if (!init || chunkSize == 0) return 0;

        if (dimIdx >= dims.size())
        {
            if (init->singleInit)
            {
                auto* single = dynamic_cast<FE::AST::Initializer*>(init);
                if (!single) return 0;
                slots.emplace_back(baseOffset, single);
                return 1;
            }

            auto* list = dynamic_cast<FE::AST::InitializerList*>(init);
            if (!list || !list->init_list) return 0;

            size_t used = 0;
            for (auto* child : *(list->init_list))
            {
                if (used >= chunkSize) break;
                used += fillArrayChunk(child, dims, dimIdx, baseOffset + used, chunkSize - used, slots);
            }
            return used > chunkSize ? chunkSize : used;
        }

        size_t dimBound = dims[dimIdx] > 0 ? static_cast<size_t>(dims[dimIdx]) : 1;
        if (dimBound == 0) dimBound = 1;
        size_t subChunk = chunkSize / dimBound;
        if (subChunk == 0) subChunk = 1;

        auto* list = init->singleInit ? nullptr : dynamic_cast<FE::AST::InitializerList*>(init);
        if (!list || !list->init_list)
        {
            return fillArrayChunk(init, dims, dimIdx + 1, baseOffset, chunkSize, slots);
        }

        size_t used = 0;
        for (auto* child : *(list->init_list))
        {
            if (!child || used >= chunkSize) break;

            if (!child->singleInit)
            {
                size_t targetIdx = subChunk ? (used / subChunk) : 0;
                if (targetIdx >= dimBound) break;
                size_t chunkBase = baseOffset + targetIdx * subChunk;
                fillArrayChunk(child, dims, dimIdx + 1, chunkBase, subChunk, slots);
                used = (targetIdx + 1) * subChunk;
                continue;
            }

            size_t consumed = fillArrayChunk(child, dims, dimIdx + 1, baseOffset + used, chunkSize - used, slots);
            used += consumed;
        }

        return used > chunkSize ? chunkSize : used;
    }

    void ASTCodeGen::gatherArrayInitializers(FE::AST::InitDecl* init, const std::vector<int>& dims,
        std::vector<std::pair<size_t, FE::AST::Initializer*>>& slots)
    {
        slots.clear();
        if (!init || dims.empty()) return;
        size_t total = 1;
        for (int dim : dims)
        {
            size_t len = dim > 0 ? static_cast<size_t>(dim) : 1;
            total *= len;
        }

        if (total == 0) return;
        fillArrayChunk(init, dims, 0, 0, total, slots);
        for (auto it = slots.begin(); it != slots.end();)
        {
            if (it->first >= total)
                it = slots.erase(it);
            else
                ++it;
        }
    }

    void ASTCodeGen::visit(FE::AST::VarDeclaration& node, Module* m)
    {
        // TODO(Lab 3-2): 生成变量声明 IR（alloca、数组零初始化、可选初始化表达式）
        if (!curFunc || !node.decls) return;  // 确保在函数内且有声明列表

        DataType elemType = convert(node.type);  // 获取变量元素类型

        for (auto* vd : *(node.decls))
        {
            // 遍历变量声明语句
            if (!vd) continue;
            auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(vd->lval);  // 获取左值表达式
            if (!lval || !lval->entry) continue;

            std::vector<int> dims = vd->declDims;
            size_t           allocaReg = getNewRegId();
            AllocaInst*      allocaInst = dims.empty() ? createAllocaInst(elemType, allocaReg)
                                                       : createAllocaInst(elemType, allocaReg, dims);
            insertAllocaInst(allocaInst);

            name2reg.addSymbol(lval->entry, allocaReg);  // 插入符号表

            // 储存变量信息
            FE::AST::VarAttr attr(node.type, node.isConstDecl, scopeDepth);
            attr.arrayDims      = dims;
            reg2attr[allocaReg] = attr;

            Operand* ptr = getRegOperand(allocaReg);

            if (vd->init)
            {
                if (dims.empty())
                {
                    auto* init = dynamic_cast<FE::AST::Initializer*>(vd->init);
                    if (init && init->init_val)
                    {
                        apply(*this, *init->init_val, m);
                        size_t   initReg  = getMaxReg();
                        DataType initType = convert(init->init_val->attr.val.value.type);
                        auto     convInsts = createTypeConvertInst(initType, elemType, initReg);
                        if (!convInsts.empty())
                        {
                            initReg = getMaxReg();
                            for (auto* inst : convInsts) insert(inst);
                        }
                        insert(createStoreInst(elemType, initReg, ptr));
                    }
                }
                else
                {
                    emitArrayZeroInit(ptr, elemType, dims);

                    size_t totalElems = 1;  // 计算数组总元素数量
                    for (int dim : dims)
                    {
                        size_t len = dim > 0 ? static_cast<size_t>(dim) : 1;
                        totalElems *= len;
                    }
                    if (totalElems == 0) totalElems = 1;

                    std::vector<std::pair<size_t, FE::AST::Initializer*>> initSlots;
                    gatherArrayInitializers(vd->init, dims, initSlots);
                    if (initSlots.empty()) continue;

                    std::vector<size_t> strides(dims.size(), 1);
                    if (dims.size() >= 2)
                    {
                        for (int idx = static_cast<int>(dims.size()) - 2; idx >= 0; --idx)
                        {
                            size_t len = dims[idx + 1] > 0 ? static_cast<size_t>(dims[idx + 1]) : 1;
                            strides[idx] = strides[idx + 1] * len;
                        }
                    }

                    for (const auto& slot : initSlots)
                    {
                        size_t offset = slot.first;
                        auto*  initNode = slot.second;
                        if (!initNode || !initNode->init_val || offset >= totalElems) continue;

                        apply(*this, *initNode->init_val, m);
                        size_t   valueReg  = getMaxReg();
                        DataType valueType = convert(initNode->init_val->attr.val.value.type);
                        if (valueType != elemType && valueType != DataType::PTR && elemType != DataType::PTR)
                        {
                            auto convInsts = createTypeConvertInst(valueType, elemType, valueReg);
                            if (!convInsts.empty())
                            {
                                valueReg = getMaxReg();
                                for (auto* inst : convInsts) insert(inst);
                            }
                        }

                        std::vector<Operand*> idxOps;  // 索引操作数
                        idxOps.reserve(dims.size() + 1);
                        idxOps.push_back(getImmeI32Operand(0));

                        size_t remaining = offset;  // 当前的偏移量
                        for (size_t dimIdx = 0; dimIdx < dims.size(); ++dimIdx)
                        {
                            size_t stride = dimIdx < strides.size() ? strides[dimIdx] : 1;
                            if (stride == 0) stride = 1;
                            size_t idxVal = stride ? remaining / stride : 0;
                            idxOps.push_back(getImmeI32Operand(static_cast<int>(idxVal)));
                            remaining -= idxVal * stride;
                        }

                        size_t gepReg = getNewRegId();  // GEP 结果寄存器
                        insert(createGEP_I32Inst(elemType, ptr, dims, idxOps, gepReg));
                        insert(createStoreInst(elemType, valueReg, getRegOperand(gepReg)));
                    }
                }
            }
            else if (dims.empty())
            {
                // 单变量 无初始化 则零初始化
                Operand* zero = (elemType == DataType::F32) ? static_cast<Operand*>(getImmeF32Operand(0.0f))
                                                            : static_cast<Operand*>(getImmeI32Operand(0));
                insert(createStoreInst(elemType, zero, ptr));
            }
            // 没有初始化的数组变量，默认不处理，保持未初始化状态
        }
    }
}  // namespace ME
