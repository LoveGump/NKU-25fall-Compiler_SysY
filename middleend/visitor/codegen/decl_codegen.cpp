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

    /**
     * @brief 递归填充数组初始化表达式的偏移量与初始化器映射
     * @param init 初始化声明节点
     * @param dims 数组维度列表
     * @param dimIdx 当前处理的维度索引
     * @param baseOffset 当前递归层级对应的一维数组起始偏移
     * @param chunkSize 当前维度块大小
     * @param slots 输出参数，存储偏移量与初始化表达式的对应关系
     * @return 已填充的元素数量
     */
    size_t ASTCodeGen::fillArrayChunk(FE::AST::InitDecl* init, const std::vector<int>& dims, size_t dimIdx,
        size_t baseOffset, size_t chunkSize, std::vector<std::pair<size_t, FE::AST::Initializer*>>& slots)
    {
        // 递归填充数组初始化表达式的偏移量与初始化器映射
        if (!init || chunkSize == 0) return 0;

        if (dimIdx >= dims.size())
        {
            // 已经处理完所有维度，当前是标量位置
            if (init->singleInit)
            {
                // 单个初始化表达式
                auto* single = dynamic_cast<FE::AST::Initializer*>(init);
                if (!single) return 0;
                slots.emplace_back(baseOffset, single);
                return 1;
            }
            
            // 在标量位置遇到初始化列表，只取第一个元素，其余舍弃
            auto* list = dynamic_cast<FE::AST::InitializerList*>(init);
            if (!list || !list->init_list || list->init_list->empty()) return 0;
            
            // 只处理第一个元素，其余舍弃
            auto* firstChild = list->init_list->front();
            if (!firstChild) return 0;
            
            return fillArrayChunk(firstChild, dims, dimIdx, baseOffset, chunkSize, slots);
        }

        // 计算当前维度的边界和子块大小
        size_t dimBound = dims[dimIdx] > 0 ? static_cast<size_t>(dims[dimIdx]) : 1;
        size_t subChunk = dimBound ? chunkSize / dimBound : chunkSize;
        if (subChunk == 0) subChunk = 1;

        // 处理单个初始化表达式（非列表）
        if (init->singleInit)
        {
            // 单个初始化表达式，逐个填充
            return fillArrayChunk(init, dims, dimIdx + 1, baseOffset, chunkSize, slots);
        }

        // 处理初始化列表
        auto* list = dynamic_cast<FE::AST::InitializerList*>(init);
        if (!list || !list->init_list)
        {
            return fillArrayChunk(init, dims, dimIdx + 1, baseOffset, chunkSize, slots);
        }

        size_t used = 0; // 已使用的元素数量
        for (auto* child : *(list->init_list))
        {
            if (!child) break;
            
            // 超量舍弃：如果已经填满，直接舍弃剩余元素
            if (used >= chunkSize)
            {
                break;
            }

            if (!child->singleInit)
            {
                // 子元素是初始化列表
                // 判断当前位置是否在某个子数组的边界
                
                bool isAtBoundary = false;
                size_t boundarySubChunk = 0;
                
                // 特殊情况：如果 subChunk == 1，说明已经到了标量层级
                // 此时遇到初始化列表应该只取第一个元素
                if (subChunk == 1)
                {
                    isAtBoundary = false;
                }
                else
                {
                    // 从当前维度之后开始，计算每个可能的子数组大小
                    // 从大到小检查，优先匹配更大的子数组
                    // 例如对于 arr[3][3][3]，在 dimIdx=0 时：
                    // 先检查 dims[1]*dims[2] = 9 (arr[i] 的大小)
                    // 再检查 dims[2] = 3 (arr[i][j] 的大小)
                    
                    // 先计算所有维度的乘积
                    std::vector<size_t> subChunkSizes;
                    size_t accumSize = 1;
                    for (size_t testDim = dims.size() - 1; testDim > dimIdx; --testDim)
                    {
                        accumSize *= static_cast<size_t>(dims[testDim]);
                        if (accumSize > 1)
                        {
                            subChunkSizes.push_back(accumSize);
                        }
                    }
                    
                    // 从大到小检查
                    for (auto it = subChunkSizes.rbegin(); it != subChunkSizes.rend(); ++it)
                    {
                        if (used % (*it) == 0)
                        {
                            isAtBoundary = true;
                            boundarySubChunk = *it;
                            break;
                        }
                    }
                    
                    // 如果在当前维度的子块边界
                    if (!isAtBoundary && subChunk > 1 && used % subChunk == 0)
                    {
                        isAtBoundary = true;
                        boundarySubChunk = subChunk;
                    }
                }
                
                if (isAtBoundary && boundarySubChunk > 0)
                {
                    // 在子数组边界遇到初始化列表
                    // 将其用于初始化该子数组
                    size_t avail = chunkSize - used;
                    size_t segSize = std::min(boundarySubChunk, avail);
                    size_t chunkBase = baseOffset + used;
                    
                    if (segSize == 0) break;
                    
                    // 递归处理，内部的超量元素会被舍弃
                    fillArrayChunk(child, dims, dimIdx + 1, chunkBase, segSize, slots);
                    // 即使实际填充少于 segSize，也要占据整个子数组
                    used += segSize;
                }
                else
                {
                    // 在标量位置遇到初始化列表
                    // 只取列表的第一个元素
                    auto* childList = dynamic_cast<FE::AST::InitializerList*>(child);
                    if (childList && childList->init_list && !childList->init_list->empty())
                    {
                        auto* firstElem = childList->init_list->front();
                        if (firstElem)
                        {
                            size_t chunkBase = baseOffset + used;
                            size_t avail = chunkSize - used;
                            size_t consumed = fillArrayChunk(firstElem, dims, dimIdx + 1, chunkBase, avail, slots);
                            used += consumed;
                        }
                    }
                }
            }
            else
            {
                // 子元素是单个初始化表达式
                size_t avail = chunkSize - used;
                size_t consumed = fillArrayChunk(child, dims, dimIdx + 1, baseOffset + used, avail, slots);
                used += consumed;
            }
        }

        return used;
    }


    void ASTCodeGen::gatherArrayInitializers(FE::AST::InitDecl* init, const std::vector<int>& dims,
        std::vector<std::pair<size_t, FE::AST::Initializer*>>& slots)
    {
        // 清空存储槽，准备收集新的初始化表达式及其偏移量
        slots.clear();
        if (!init || dims.empty()) return;  // 如果初始化声明为空或维度列表为空，直接返回
        // 计算数组的总元素数量
        size_t total = 1;
        for (int dim : dims)
        {
            size_t len = dim > 0 ? static_cast<size_t>(dim) : 1;
            total *= len;
        }

        fillArrayChunk(init, dims, 0, 0, total, slots);
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

            std::vector<int> dims      = vd->declDims;
            size_t           allocaReg = getNewRegId();  // 分配新的寄存器用于存储 alloca 结果
            if (dims.empty()) { insertAllocaInst(createAllocaInst(elemType, allocaReg)); }
            else
            {
                insertAllocaInst(createAllocaInst(elemType, allocaReg, dims));
            }
            name2reg.addSymbol(lval->entry, allocaReg);  // 插入符号表

            // 储存变量信息
            FE::AST::VarAttr attr(node.type, node.isConstDecl, scopeDepth);
            attr.arrayDims      = dims;
            reg2attr[allocaReg] = attr;  // 在表中记录变量属性

            Operand* ptr = getRegOperand(allocaReg);  // 获取申请出的地址

            if (vd->init)
            {
                // 处理初始化表达式
                if (dims.empty())
                {
                    // 单变量初始化
                    auto* init = dynamic_cast<FE::AST::Initializer*>(vd->init);
                    if (init && init->init_val)
                    {
                        // 生成初始化表达式的ir
                        apply(*this, *init->init_val, m);
                        size_t   initReg   = getMaxReg();
                        DataType initType  = convert(init->init_val->attr.val.value.type);
                        auto     convInsts = createTypeConvertInst(initType, elemType, initReg);
                        if (!convInsts.empty())
                        {
                            initReg = getMaxReg();
                            for (auto* inst : convInsts) { insert(inst); }
                        }
                        insert(createStoreInst(elemType, initReg, ptr));
                    }
                }
                else
                {
                    size_t totalElems = 1;  // 计算数组总元素数量
                    for (int dim : dims)
                    {
                        size_t len = static_cast<size_t>(dim);
                        totalElems *= len;
                    }
                    // 首先进行数组零初始化
                    emitArrayZeroInit(ptr, elemType, totalElems);

                    std::vector<std::pair<size_t, FE::AST::Initializer*>> initSlots;
                    gatherArrayInitializers(vd->init, dims, initSlots);
                    if (initSlots.empty()) continue;

                    // 再往数组中填充初始化表达式的值
                    std::vector<size_t> strides(dims.size(), 1);
                    if (dims.size() >= 2)
                    {  // 至少两维时计算步长
                        for (int idx = static_cast<int>(dims.size()) - 2; idx >= 0; --idx)
                        {
                            size_t len   = static_cast<size_t>(dims[idx + 1]);
                            strides[idx] = strides[idx + 1] * len;
                        }
                    }

                    // 遍历所有初始化表达式，生成对应的初始值
                    for (const auto& slot : initSlots)
                    {
                        size_t offset   = slot.first;  // 平铺后的偏移量
                        auto*  initNode = slot.second;
                        if (!initNode || !initNode->init_val) continue;

                        apply(*this, *initNode->init_val, m);  // 生成初始化表达式的ir
                        size_t   valueReg  = getMaxReg();      // 获取初始化值寄存器
                        DataType valueType = convert(initNode->init_val->attr.val.value.type);
                        if (valueType != elemType && valueType != DataType::PTR && elemType != DataType::PTR)
                        {
                            // 需要类型转换
                            auto convInsts = createTypeConvertInst(valueType, elemType, valueReg);
                            if (!convInsts.empty())
                            {
                                valueReg = getMaxReg();
                                for (auto* inst : convInsts) insert(inst);
                            }
                        }

                        std::vector<Operand*> idxOps;  // 计算GEP的索引操作数
                        idxOps.reserve(dims.size() + 1);
                        idxOps.push_back(getImmeI32Operand(0));  // 首个维度为0，表示从数组首地址开始

                        size_t remaining = offset;  // 计算剩余偏移量
                        for (size_t dimIdx = 0; dimIdx < dims.size(); ++dimIdx)
                        {
                            size_t stride = strides[dimIdx];     // 当前维度的步长
                            size_t idxVal = remaining / stride;  // 计算当前层维度的索引值
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
                if (elemType == DataType::F32) { insert(createStoreInst(elemType, getImmeF32Operand(0.0f), ptr)); }
                else
                {
                    insert(createStoreInst(elemType, getImmeI32Operand(0), ptr));
                }
            }
            // 没有初始化的数组变量，默认不处理，保持未初始化状态
        }
    }
}  // namespace ME
