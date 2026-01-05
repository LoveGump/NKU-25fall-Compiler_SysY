/**
 * @file dag_builder.cpp
 * @brief DAGBuilder 的实现 - 将 LLVM IR 转换为 SelectionDAG
 *
 * 本文件实现了 DAGBuilder 类的所有 visit 方法，每个方法负责
 * 处理一种特定的 IR 指令，并创建相应的 DAG 节点。
 *
 * 核心设计：
 * 1. 值映射管理：通过 reg_value_map_ 维护 IR 虚拟寄存器到 DAG 节点的映射
 * 2. Chain 依赖管理：通过 currentChain_ 串联所有副作用操作
 * 3. 节点去重：所有节点创建都通过 SelectionDAG::getNode()，自动实现 CSE
 *
 * 关键方法：
 * - getValue(): 获取 IR 操作数对应的 DAG 节点
 * - setDef(): 记录 IR 指令的定义结果
 * - visit(XxxInst&): 为每种 IR 指令创建对应的 DAG 节点
 *
 * @brief 若有兴趣了解更多LLVM中DAG的具体内容，可参考
 * https://llvm.org/devmtg/2024-10/slides/tutorial/MacLean-Fargnoli-ABeginnersGuide-to-SelectionDAG.pdf
 * or
 * https://zhuanlan.zhihu.com/p/600170077
 */

#include <backend/dag/dag_builder.h>
#include <cstdint>
#include <algorithm>
#include <debug.h>

namespace BE
{
    namespace DAG
    {
        static inline bool isFloatType(ME::DataType t) { return t == ME::DataType::F32 || t == ME::DataType::DOUBLE; }

        // 将 ME::DataType 转换为 BE::DataType
        BE::DataType* DAGBuilder::mapType(ME::DataType t)
        {
            switch (t)
            {
                case ME::DataType::I1:
                case ME::DataType::I8:
                case ME::DataType::I32: return BE::I32;
                case ME::DataType::I64:
                case ME::DataType::PTR: return BE::I64;
                case ME::DataType::F32: return BE::F32;
                case ME::DataType::DOUBLE: return BE::F64;
                default: ERROR("Unsupported IR data type"); return BE::I32;
            }
        }

        // 访问 ME::Module，构建 SelectionDAG
        void DAGBuilder::visit(ME::Module& module, SelectionDAG& dag)
        {
            // Module 级别的 DAG 构建：遍历函数
            // 注意：全局变量在 DAG 阶段不需要处理，由 ISel 的 importGlobals 处理
            for (auto* func : module.functions)
            {
                apply(*this, *func, dag);
            }
            //这里
        }
        void DAGBuilder::visit(ME::Function& func, SelectionDAG& dag)
        {
            // Function 级别的 DAG 构建：遍历基本块
            // 每个函数共享寄存器映射，确保跨基本块使用同一 SSA 寄存器
            reg_value_map_.clear();
            alloca_map_.clear();
            for (auto& [blockId, block] : func.blocks)
            {
                apply(*this, *block, dag);
            }
        }

        // 获取 IR 操作数对应的 DAG 节点
        SDValue DAGBuilder::getValue(ME::Operand* op, SelectionDAG& dag, BE::DataType* dtype)
        {
            // 如果操作数为空，返回空的 SDValue
            if (!op) return SDValue();
            // 根据操作数类型进行分支处理
            switch (op->getType())
            {
                // 处理寄存器操作数
                case ME::OperandType::REG:
                {
                    // 寄存器操作数必须提供目标数据类型
                    ASSERT(dtype != nullptr && "dtype required for REG operands");

                    // 获取寄存器编号
                    size_t id = op->getRegNum();
                    // 检查是否已经存在该寄存器的映射，若存在则直接返回
                    auto   it = reg_value_map_.find(id);
                    if (it != reg_value_map_.end()) return it->second;

                    // 若不存在，则在 DAG 中创建一个新的寄存器节点
                    SDValue v = dag.getRegNode(id, dtype);

                    // 缓存映射关系并返回
                    reg_value_map_[id] = v;
                    return v;
                }
                // 处理 32 位整型立即数
                case ME::OperandType::IMMEI32:
                {
                    // 提取立即数值并创建 I32 类型的常量节点
                    auto imm = static_cast<ME::ImmeI32Operand*>(op)->value;
                    return dag.getConstantI64(imm, BE::I32);
                }
                // 处理 32 位浮点型立即数
                case ME::OperandType::IMMEF32:
                {
                    // 提取立即数值并创建 F32 类型的常量节点
                    auto imm = static_cast<ME::ImmeF32Operand*>(op)->value;
                    return dag.getConstantF32(imm, BE::F32);
                }
                // 处理全局变量操作数
                case ME::OperandType::GLOBAL:
                {
                    // 将全局符号映射为 SYMBOL 节点并返回
                    auto* gop = static_cast<ME::GlobalOperand*>(op);
                    return dag.getSymNode(static_cast<uint32_t>(ISD::SYMBOL), {BE::PTR}, {}, gop->name);
                }
                // 处理标签操作数
                case ME::OperandType::LABEL:
                {
                    // 将标签操作数映射为 LABEL 节点（立即数携带标签 id）
                    auto* lop = static_cast<ME::LabelOperand*>(op);
                    return dag.getImmNode(static_cast<uint32_t>(ISD::LABEL), {}, {}, static_cast<int64_t>(lop->lnum));
                }
                // 遇到不支持的操作数类型，触发错误
                default: ERROR("Unsupported IR operand in DAGBuilder"); return SDValue();
            }
        }

        // 设置操作数的定义结果
        void DAGBuilder::setDef(ME::Operand* res, const SDValue& val)
        {
            // 检查操作数是否有效且为寄存器类型
            if (!res || res->getType() != ME::OperandType::REG) return;

            // 获取寄存器 ID 并更新寄存器到 SDValue 的映射
            size_t regId          = res->getRegNum();
            reg_value_map_[regId] = val;

            // 如果 SDValue 关联了节点，则在节点中记录对应的 IR 寄存器 ID
            if (val.getNode()) val.getNode()->setIRRegId(regId);
        }

        // 将 ME::Operator 映射为 ISD::Operator
        uint32_t DAGBuilder::mapArithmeticOpcode(ME::Operator op, bool isFloat)
        {
            if (isFloat)
            {
                switch (op)
                {
                    case ME::Operator::FADD: return static_cast<uint32_t>(ISD::FADD);
                    case ME::Operator::FSUB: return static_cast<uint32_t>(ISD::FSUB);
                    case ME::Operator::FMUL: return static_cast<uint32_t>(ISD::FMUL);
                    case ME::Operator::FDIV: return static_cast<uint32_t>(ISD::FDIV);
                    default: ERROR("Unsupported float arithmetic opcode"); return static_cast<uint32_t>(ISD::FADD);
                }
            }
            switch (op)
            {
                case ME::Operator::ADD: return static_cast<uint32_t>(ISD::ADD);
                case ME::Operator::SUB: return static_cast<uint32_t>(ISD::SUB);
                case ME::Operator::MUL: return static_cast<uint32_t>(ISD::MUL);
                case ME::Operator::DIV: return static_cast<uint32_t>(ISD::DIV);
                case ME::Operator::MOD: return static_cast<uint32_t>(ISD::MOD);
                case ME::Operator::SHL: return static_cast<uint32_t>(ISD::SHL);
                case ME::Operator::ASHR: return static_cast<uint32_t>(ISD::ASHR);
                case ME::Operator::LSHR: return static_cast<uint32_t>(ISD::LSHR);
                case ME::Operator::BITAND: return static_cast<uint32_t>(ISD::AND);
                case ME::Operator::BITXOR: return static_cast<uint32_t>(ISD::XOR);
                default: ERROR("Unsupported integer arithmetic opcode"); return static_cast<uint32_t>(ISD::ADD);
            }
        }

        // 访问 ME::Block，构建 SelectionDAG
        void DAGBuilder::visit(ME::Block& block, SelectionDAG& dag)
        {
            // 记录当前基本块的入口令牌
            currentChain_ = dag.getNode(static_cast<unsigned>(ISD::ENTRY_TOKEN), {BE::TOKEN}, {});
            // 遍历基本块中的指令
            for (auto* inst : block.insts) apply(*this, *inst, dag);
        }

        // 访问 ME::RetInst，构建 SelectionDAG
        void DAGBuilder::visit(ME::RetInst& inst, SelectionDAG& dag)
        {
            // 构建返回指令的操作数列表
            std::vector<SDValue> ops;

            // 结合此处考虑 currentChain_ 的作用是什么
            // currentChain 的作用是：
            // 记录当前的副作用序列，确保有副作用的操作（如 STORE、CALL、RET）
            // 按照程序语义正确的顺序执行，防止被调度器重排。
            // 确保返回指令在所有副作用操作之后执行，符合程序语义。
            ops.push_back(currentChain_);

            // 如果存在返回值，则将其转换为 DAG 节点并加入操作数列表
            if (inst.res != nullptr)
            {
                SDValue v = getValue(inst.res, dag, mapType(inst.rt));
                ops.push_back(v);
            }
            // 在 DAG 中创建 RET 节点，RET 节点通常作为基本块的终结节点
            dag.getNode(static_cast<unsigned>(ISD::RET), {}, ops);
        }

        //这是处理 IR Load 指令 的函数，将其转换为 DAG LOAD 节点：
        void DAGBuilder::visit(ME::LoadInst& inst, SelectionDAG& dag)
        {
            // 1. 获取加载数据的类型（i32 → BE::I32）
            auto    vt  = mapType(inst.dt);
            // 2. 获取地址操作数对应的 DAG 节点
            SDValue ptr = getValue(inst.ptr, dag, BE::PTR);

            // LOAD: (Chain, Address) -> (Value, Chain)
            // 3. 创建 LOAD 节点
            // LOAD 节点的特点：
            //   输入：[Chain, Address]
            //   输出：[Value, Chain]  ← 有两个输出！
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::LOAD), {vt, BE::TOKEN}, {currentChain_, ptr});
            // 4. 记录加载的值（结果 #0）到 reg_value_map_
            setDef(inst.res, SDValue(node.getNode(), 0));  // Value is result #0
            // 5. 更新 currentChain_ 为 LOAD 节点的 Chain 输出
            currentChain_ = SDValue(node.getNode(), 1);    // Chain is result #1
        }

        //这是处理 IR Store 指令 的函数，将其转换为 DAG STORE 节点：
        void DAGBuilder::visit(ME::StoreInst& inst, SelectionDAG& dag)
        {
            // 1. 获取值操作数对应的 DAG 节点
            SDValue val = getValue(inst.val, dag, mapType(inst.dt));
            // 2. 获取地址操作数对应的 DAG 节点
            SDValue ptr = getValue(inst.ptr, dag, BE::PTR);
            // 3. 创建 STORE 节点
            // STORE 节点的特点：
            //   输入：[Chain, Value, Address]
            //   输出：[Chain]
            SDValue storeNode = dag.getNode(static_cast<unsigned>(ISD::STORE), {BE::TOKEN}, {currentChain_, val, ptr});
            // 4. 更新 currentChain_ 为 STORE 节点的 Chain 输出
            currentChain_ = storeNode;
        }

        //这是处理 IR 算术指令 的函数，将其转换为 DAG 算术节点：
        void DAGBuilder::visit(ME::ArithmeticInst& inst, SelectionDAG& dag)
        {
            // 1. 判断是否为浮点数类型
            bool     f    = isFloatType(inst.dt);
            // 2. 获取操作数类型
            auto     vt   = mapType(inst.dt);
            // 3. 获取左操作数对应的 DAG 节点
            SDValue  lhs  = getValue(inst.lhs, dag, vt);
            SDValue  rhs  = getValue(inst.rhs, dag, vt);
            uint32_t opc  = mapArithmeticOpcode(inst.opcode, f);
            // 4. 创建算术节点
            SDValue  node = dag.getNode(opc, {vt}, {lhs, rhs});
            // 5. 设置结果寄存器
            setDef(inst.res, node);
        }

        void DAGBuilder::visit(ME::IcmpInst& inst, SelectionDAG& dag)
        {
            // 生成 ICMP 节点，结果类型为 I32，携带比较条件 imm
            auto    vt  = mapType(inst.dt);
            SDValue lhs = getValue(inst.lhs, dag, vt);
            SDValue rhs = getValue(inst.rhs, dag, vt);
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::ICMP), {BE::I32}, {lhs, rhs});
            node.getNode()->setImmI64(static_cast<int64_t>(inst.cond));
            setDef(inst.res, node);
        }

        void DAGBuilder::visit(ME::FcmpInst& inst, SelectionDAG& dag)
        {
            // 生成 FCMP 节点，结果类型为 I32，携带浮点比较条件 imm
            auto    vt  = mapType(inst.dt);
            SDValue lhs = getValue(inst.lhs, dag, vt);
            SDValue rhs = getValue(inst.rhs, dag, vt);
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::FCMP), {BE::I32}, {lhs, rhs});
            node.getNode()->setImmI64(static_cast<int64_t>(inst.cond));
            setDef(inst.res, node);
        }

        //为alloca指令生成DAG节点，对于没有优化的部分就用这个
        void DAGBuilder::visit(ME::AllocaInst& inst, SelectionDAG& dag)
        {
            // 1. 获取结果寄存器ID
            size_t dest_id = static_cast<ME::RegOperand*>(inst.res)->getRegNum();

            // 2. 创建 FrameIndex 节点
            DataType* ptr_ty = BE::I64;
            SDValue   v      = dag.getFrameIndexNode(static_cast<int>(dest_id), ptr_ty);

            // 3. 设置 IR 寄存器ID
            v.getNode()->setIRRegId(dest_id);

            // 4. 记录结果寄存器
            reg_value_map_[dest_id] = v;
            // 5. 记录 alloca 指令
            alloca_map_[dest_id]    = v;
        }

        void DAGBuilder::visit(ME::BrCondInst& inst, SelectionDAG& dag)
        {
            // 生成 BRCOND 节点
            // 操作数顺序: [cond(I32), trueLabel(LABEL), falseLabel(LABEL)]
            SDValue cond = getValue(inst.cond, dag, BE::I32);
            SDValue trueLabel = getValue(inst.trueTar, dag, nullptr);
            SDValue falseLabel = getValue(inst.falseTar, dag, nullptr);
            dag.getNode(static_cast<unsigned>(ISD::BRCOND), {}, {cond, trueLabel, falseLabel});
        }

        void DAGBuilder::visit(ME::BrUncondInst& inst, SelectionDAG& dag)
        {
            // 生成无条件分支 BR 节点，操作数为目标 LABEL
            SDValue target = getValue(inst.target, dag, nullptr);
            dag.getNode(static_cast<unsigned>(ISD::BR), {}, {target});
        }

        void DAGBuilder::visit(ME::GlbVarDeclInst& inst, SelectionDAG& dag)
        {
            (void)inst;
            (void)dag;
            ERROR("GlbVarDeclInst should not appear in DAGBuilder");
        }

        void DAGBuilder::visit(ME::CallInst& inst, SelectionDAG& dag)
        {
            // 生成 CALL 节点并维护 Chain
            std::vector<SDValue> ops;
            // 1) ops 以 currentChain_ 开头，保证调用顺序
            ops.push_back(currentChain_);
            // 2) 追加 callee 符号
            SDValue callee = dag.getSymNode(static_cast<uint32_t>(ISD::SYMBOL), {BE::PTR}, {}, inst.funcName);
            ops.push_back(callee);
            // 3) 依次追加所有参数
            for (auto& [argType, argOp] : inst.args)
            {
                SDValue arg = getValue(argOp, dag, mapType(argType));
                ops.push_back(arg);
            }
            // 4) 生成 CALL 节点
            if (inst.retType != ME::DataType::VOID && inst.res != nullptr)
            {
                // 有返回值
                auto vt = mapType(inst.retType);
                SDValue node = dag.getNode(static_cast<unsigned>(ISD::CALL), {vt, BE::TOKEN}, ops);
                setDef(inst.res, SDValue(node.getNode(), 0));
                currentChain_ = SDValue(node.getNode(), 1);
            }
            else
            {
                // 无返回值
                SDValue node = dag.getNode(static_cast<unsigned>(ISD::CALL), {BE::TOKEN}, ops);
                currentChain_ = node;
            }
        }

        void DAGBuilder::visit(ME::FuncDeclInst& inst, SelectionDAG& dag)
        {
            (void)inst;
            (void)dag;
            ERROR("FuncDeclInst should not appear in DAGBuilder");
        }
        void DAGBuilder::visit(ME::FuncDefInst& inst, SelectionDAG& dag)
        {
            (void)inst;
            (void)dag;
            ERROR("FuncDefInst should not appear in DAGBuilder");
        }

        [[maybe_unused]]
        static inline int elemByteSize(ME::DataType t)
        {
            switch (t)
            {
                case ME::DataType::I1:
                case ME::DataType::I8:
                case ME::DataType::I32: return 4;
                case ME::DataType::I64:
                case ME::DataType::PTR: return 8;
                case ME::DataType::F32: return 4;
                case ME::DataType::DOUBLE: return 8;
                default: return 4;
            }
        }

        void DAGBuilder::visit(ME::GEPInst& inst, SelectionDAG& dag)
        {
            // 实现 GEP 到地址计算 DAG 的转换
            // 1) 取 base 指针
            SDValue base = getValue(inst.basePtr, dag, BE::PTR);
            
            // 计算元素大小
            int elemSize = elemByteSize(inst.dt);
            
            // 2) 计算偏移: 对于多维数组需要按维度展开
            // dims 存储了各维度大小，idxs 存储了各维度索引
            // 例如 a[i][j] 对于 [M x N] 数组，偏移 = (i * N + j) * elemSize
            SDValue totalOffset;
            bool hasOffset = false;
            
            // 预计算后缀乘积 stride，suffixProd[k] = dims[k] * dims[k+1] * ...
            std::vector<int> suffixProd(inst.dims.size() + 1, 1);
            for (int i = static_cast<int>(inst.dims.size()) - 1; i >= 0; --i)
            {
                suffixProd[i] = suffixProd[i + 1] * inst.dims[i];
            }

            // LLVM/GEP-style索引通常会带一个 leading idx（对 alloca/全局数组常为 0），
            // 当 idx 数量不少于维度数时需要将第一个 idx 单独处理。
            bool hasLeadingIdx = inst.idxs.size() >= inst.dims.size();

            for (size_t i = 0; i < inst.idxs.size(); ++i)
            {
                SDValue idx = getValue(inst.idxs[i], dag, BE::I64);

                size_t dimIdx;
                if (hasLeadingIdx)
                {
                    // idx0 对应整个数组，步长为全维度乘积
                    if (i == 0)
                        dimIdx = 0;
                    else
                        dimIdx = i - 1;
                }
                else
                    dimIdx = i;

                int elemStride = 1;
                if (hasLeadingIdx && i == 0)
                    elemStride = suffixProd[0];
                else if (dimIdx < suffixProd.size() - 1)
                    elemStride = suffixProd[dimIdx + 1];

                int      byteStride = elemStride * elemSize;
                SDValue  strideNode = dag.getConstantI64(byteStride, BE::I64);
                SDValue  offset     = dag.getNode(static_cast<unsigned>(ISD::MUL), {BE::I64}, {idx, strideNode});

                if (!hasOffset)
                {
                    totalOffset = offset;
                    hasOffset   = true;
                }
                else
                {
                    totalOffset = dag.getNode(static_cast<unsigned>(ISD::ADD), {BE::I64}, {totalOffset, offset});
                }
            }
            
            // 3) 基址 + 偏移
            SDValue result;
            if (hasOffset)
            {
                result = dag.getNode(static_cast<unsigned>(ISD::ADD), {BE::PTR}, {base, totalOffset});
            }
            else
            {
                result = base;
            }
            
            setDef(inst.res, result);
        }

        void DAGBuilder::visit(ME::ZextInst& inst, SelectionDAG& dag)
        {
            // 实现零扩展 ZEXT（from -> to）
            auto srcType = mapType(inst.from);
            auto dstType = mapType(inst.to);
            SDValue src = getValue(inst.src, dag, srcType);
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::ZEXT), {dstType}, {src});
            setDef(inst.dest, node);
        }

        void DAGBuilder::visit(ME::SI2FPInst& inst, SelectionDAG& dag)
        {
            // 实现 SITOFP（有符号整型到浮点）
            SDValue src = getValue(inst.src, dag, BE::I32);
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::SITOFP), {BE::F32}, {src});
            setDef(inst.dest, node);
        }

        void DAGBuilder::visit(ME::FP2SIInst& inst, SelectionDAG& dag)
        {
            // 实现 FPTOSI（浮点到有符号整型）
            SDValue src = getValue(inst.src, dag, BE::F32);
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::FPTOSI), {BE::I32}, {src});
            setDef(inst.dest, node);
        }

        void DAGBuilder::visit(ME::PhiInst& inst, SelectionDAG& dag)
        {
            // 为 PHI 构造操作数列表（成对的 LABEL 与 VALUE）
            // ops 形如 [LABEL0, VAL0, LABEL1, VAL1, ...]
            auto vt = mapType(inst.dt);//结果类型
            std::vector<SDValue> ops;//操作数列表
            
            // 1. 构造操作数列表
            for (auto& [labelOp, valOp] : inst.incomingVals)
            {
                // 2. 获取值
                SDValue val = getValue(valOp, dag, vt);
                // 3. 获取标签
                SDValue label = getValue(labelOp, dag, nullptr);
                // SelectionDAG PHI operands are [value, label] pairs
                // 4. 添加到操作数列表
                ops.push_back(val);
                ops.push_back(label);
            }
            
            // 5. 构造 PHI 节点
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::PHI), {vt}, ops);
            setDef(inst.res, node);
        }

    }  // namespace DAG
}  // namespace BE
