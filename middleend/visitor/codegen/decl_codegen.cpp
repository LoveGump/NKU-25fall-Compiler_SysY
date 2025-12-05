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
        {  // 递归到正常情况下的最后一维 ，比如 int a[2][3] = {{1,2,3},{4,5,6}};中的1，2，3，4，5，6
            if (init->singleInit)
            {
                // 处理单个初始化表达式
                auto* single = dynamic_cast<FE::AST::Initializer*>(init);
                if (!single) return 0;
                slots.emplace_back(baseOffset, single);  // 记录偏移量与初始化表达式的对应关系
                return 1;
            }
            // 数组初始化列表，也有可能是 int a[3] = { {1}, {2}, {3} };，继续递归处理
            auto* list = dynamic_cast<FE::AST::InitializerList*>(init);
            if (!list || !list->init_list) return 0;

            size_t used = 0;  // 已使用的元素数量
            for (auto* child : *(list->init_list))
            {
                // 遍历子初始化表达式
                // 超出块大小直接报错
                if (used >= chunkSize)
                {
                    ERROR("Too many initializers for array at line %d", init->line_num);
                    break;
                }
                // 递归处理子初始化表达式
                used += fillArrayChunk(child, dims, dimIdx, baseOffset + used, chunkSize - used, slots);
            }
            return used;
        }

        // 计算当前维度的边界和子块大小，
        // 对于array[2][3]，dimIdx=0时，dimBound=2，subChunk=3;
        // dimIdx=1时，dimBound=3，subChunk=1;
        size_t dimBound = dims[dimIdx];          // 维度边界
        size_t subChunk = chunkSize / dimBound;  // 计算子块大小

        // 处理非单个初始化表达式的情况
        auto* list = init->singleInit ? nullptr : dynamic_cast<FE::AST::InitializerList*>(init);
        if (!list || !list->init_list)
        {
            // 处理缺失初始化列表的情况，逐个填充子块即为int a[2][3] = {1,2,3,4,5,6 };这种情况
            return fillArrayChunk(init, dims, dimIdx + 1, baseOffset, chunkSize, slots);
        }

        size_t used = 0;  // 已使用的元素数量
        for (auto* child : *(list->init_list))
        {
            // 遍历子初始化表达式
            if (!child) break;
            if (used >= chunkSize)
            {
                ERROR("Too many initializers for array at line %d", init->line_num);
                break;
            }

            if (!child->singleInit)
            {
                // child 是一个子初始化列表 int a[2][3] = {{1,2,3},{4,5,6}}
                size_t targetIdx = used / subChunk;  // 计算目标索引
                if (targetIdx >= dimBound)
                {
                    // 如果超出维度边界，报错
                    ERROR("Too many initializers for array at line %d", init->line_num);
                    break;
                }
                size_t chunkBase = baseOffset + targetIdx * subChunk;  // 计算当前块的基地址，第几个子块
                // 递归处理子初始化列表
                fillArrayChunk(child, dims, dimIdx + 1, chunkBase, subChunk, slots);
                used = (targetIdx + 1) * subChunk;  // 更新已使用的元素数量
                continue;
            }
            // 递归处理单个初始化表达式
            used += fillArrayChunk(child, dims, dimIdx + 1, baseOffset + used, chunkSize - used, slots);
        }

        return used;
    }

    /**
     * @brief 收集数组初始化器中的所有初始化表达式及其对应的偏移量
     * @param init 初始化声明节点
     * @param dims 数组维度列表
     * @param slots 输出参数，存储偏移量与初始化表达式的对应关系
     */
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
            else { insertAllocaInst(createAllocaInst(elemType, allocaReg, dims)); }
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
                else { insert(createStoreInst(elemType, getImmeI32Operand(0), ptr)); }
            }
            // 没有初始化的数组变量，默认不处理，保持未初始化状态
        }
    }
}  // namespace ME
