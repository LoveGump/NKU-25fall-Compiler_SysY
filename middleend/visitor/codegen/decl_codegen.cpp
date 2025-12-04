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
        // TODO(Lab 3-2): 生成变量声明 IR（alloca、数组零初始化、可选初始化表达式）
        if (!curFunc || !node.decls) return;

        DataType elemType = convert(node.type);

        for (auto* vd : *(node.decls))
        {
            // 遍历变量声明语句
            if (!vd) continue;
            auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(vd->lval);
            if (!lval || !lval->entry) continue;

            // needtodo
            std::vector<int> dims = vd->declDims;

            size_t      allocaReg  = getNewRegId();  // 分配新的寄存器id
            AllocaInst* allocaInst = nullptr;        // alloca 指令
            if (dims.empty())
            {
                // 单变量
                allocaInst = createAllocaInst(elemType, allocaReg);
            }
            else
            {
                // 数组
                allocaInst = createAllocaInst(elemType, allocaReg, dims);
            }
            insertAllocaInst(allocaInst);  // 插入指令

            name2reg.addSymbol(lval->entry, allocaReg);  // 插入符号表

            // 储存变量信息
            FE::AST::VarAttr attr(node.type, node.isConstDecl, scopeDepth);
            attr.arrayDims      = dims;
            reg2attr[allocaReg] = attr;  // 加入变量属性表

            Operand* ptr = getRegOperand(allocaReg);  // 获取变量地址

            if (vd->init)
            {
                // 有初始化语句
                if (dims.empty())
                {
                    // 单变量
                    if (auto* init = dynamic_cast<FE::AST::Initializer*>(vd->init))
                    {
                        // 如果有初始化语句
                        if (init->init_val)
                        {
                            // 为初始化语句生成ir
                            apply(*this, *init->init_val, m);
                            size_t   initReg = getMaxReg();  // 获取初始化值寄存器
                            DataType initType = convert(init->init_val->attr.val.value.type);  // 获取初始化值类型
                            // needtodo
                            auto insert_insts = createTypeConvertInst(initType, elemType, initReg);
                            if (!insert_insts.empty())
                            {
                                initReg = getMaxReg();  // 使用新的寄存器来存放变量
                                for (auto inst : insert_insts)
                                {
                                    insert(inst);  // 插入类型转换指令
                                }
                            }
                            // 插入储存指令
                            insert(createStoreInst(elemType, initReg, ptr));
                        }
                    }
                }
                else
                {

                    size_t totalElems = 1;  // 计算数组总元素数量
                    for (int dim : vd->declDims) totalElems *= static_cast<size_t>(dim);
                    if (totalElems == 0) totalElems = 1;
                    // 使用初始化值填充数组
                    std::vector<FE::AST::VarValue> initValues(totalElems, makeZeroValue(node.type));
                    fillGlobalArrayInit(vd->init, node.type, vd->declDims, initValues);  // 展平填充

                    // 生成对应的指令 将展平的初始化值存入数组
                    // strides 用于计算多维数组的偏移
                    std::vector<size_t> strides(vd->declDims.size(), 1);
                    for (int idx = static_cast<int>(vd->declDims.size()) - 2; idx >= 0; --idx)
                    {
                        strides[idx] = strides[idx + 1] * static_cast<size_t>(vd->declDims[idx + 1]);
                    }

                    for (size_t offset = 0; offset < initValues.size(); ++offset)
                    {
                        // offset 是展平后的位置，这里将它还原成 GEP 需要的多维索引链
                        std::vector<Operand*> idxOps;  // 索引操作数
                        idxOps.reserve(vd->declDims.size() + 1);
                        idxOps.push_back(getImmeI32Operand(0));  // 先放基地址 0

                        size_t remaining = offset;  // 当前的偏移量
                        for (size_t dimIdx = 0; dimIdx < vd->declDims.size(); ++dimIdx)
                        {
                            // 遍历所有的维度
                            size_t stride = strides[dimIdx];                  // stride = 当前维后续元素数
                            if (stride == 0) stride = 1;                      // 防止零维度
                            size_t idxVal = stride ? remaining / stride : 0;  // 计算当前维度的索引
                            idxOps.push_back(getImmeI32Operand(static_cast<int>(idxVal)));
                            remaining -= idxVal * stride;  // 去掉已定位的这部分 offset
                        }

                        // 根据索引链发射 GEP + store，将常量初值写入栈上数组
                        size_t gepReg = getNewRegId();  // GEP 结果寄存器
                        insert(createGEP_I32Inst(elemType, ptr, vd->declDims, idxOps, gepReg));

                        Operand* valOp = nullptr;
                        if (elemType == DataType::F32) { valOp = getImmeF32Operand(initValues[offset].getFloat()); }
                        else { valOp = getImmeI32Operand(initValues[offset].getInt()); }
                        // 插入存储指令，将初始值存入数组元素
                        insert(createStoreInst(elemType, valOp, getRegOperand(gepReg)));
                    }

                    // 运行时数组初始化，因为SysY支持变量作为数组元素，所以需要运行时初始化
                    emitRuntimeArrayInit(vd->init, ptr, elemType, vd->declDims, m);
                }
            }
            else if (dims.empty())
            {
                // 单变量 无初始化 则零初始化
                Operand* zero = (elemType == DataType::F32) ? static_cast<Operand*>(getImmeF32Operand(0.0f))
                                                            : static_cast<Operand*>(getImmeI32Operand(0));
                insert(createStoreInst(elemType, zero, ptr));
            }
        }
    }
}  // namespace ME
