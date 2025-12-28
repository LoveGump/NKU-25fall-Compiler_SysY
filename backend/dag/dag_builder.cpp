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

        void DAGBuilder::visit(ME::Module& module, SelectionDAG& dag)
        {
            // Module 级别的 DAG 构建：遍历函数
            // 注意：全局变量在 DAG 阶段不需要处理，由 ISel 的 importGlobals 处理
            for (auto* func : module.functions)
            {
                apply(*this, *func, dag);
            }
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

        SDValue DAGBuilder::getValue(ME::Operand* op, SelectionDAG& dag, BE::DataType* dtype)
        {
            if (!op) return SDValue();
            switch (op->getType())
            {
                case ME::OperandType::REG:
                {
                    ASSERT(dtype != nullptr && "dtype required for REG operands");

                    size_t id = op->getRegNum();
                    auto   it = reg_value_map_.find(id);
                    if (it != reg_value_map_.end()) return it->second;

                    SDValue v = dag.getRegNode(id, dtype);

                    reg_value_map_[id] = v;
                    return v;
                }
                case ME::OperandType::IMMEI32:
                {
                    auto imm = static_cast<ME::ImmeI32Operand*>(op)->value;
                    return dag.getConstantI64(imm, BE::I32);
                }
                case ME::OperandType::IMMEF32:
                {
                    auto imm = static_cast<ME::ImmeF32Operand*>(op)->value;
                    return dag.getConstantF32(imm, BE::F32);
                }
                case ME::OperandType::GLOBAL:
                {
                    // 将全局符号映射为 SYMBOL 节点并返回
                    auto* gop = static_cast<ME::GlobalOperand*>(op);
                    return dag.getSymNode(static_cast<uint32_t>(ISD::SYMBOL), {BE::PTR}, {}, gop->name);
                }
                case ME::OperandType::LABEL:
                {
                    // 将标签操作数映射为 LABEL 节点（立即数携带标签 id）
                    auto* lop = static_cast<ME::LabelOperand*>(op);
                    return dag.getImmNode(static_cast<uint32_t>(ISD::LABEL), {}, {}, static_cast<int64_t>(lop->lnum));
                }
                default: ERROR("Unsupported IR operand in DAGBuilder"); return SDValue();
            }
        }

        void DAGBuilder::setDef(ME::Operand* res, const SDValue& val)
        {
            if (!res || res->getType() != ME::OperandType::REG) return;
            size_t regId          = res->getRegNum();
            reg_value_map_[regId] = val;
            if (val.getNode()) val.getNode()->setIRRegId(regId);
        }

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

        void DAGBuilder::visit(ME::Block& block, SelectionDAG& dag)
        {
            currentChain_ = dag.getNode(static_cast<unsigned>(ISD::ENTRY_TOKEN), {BE::TOKEN}, {});
            for (auto* inst : block.insts) apply(*this, *inst, dag);
        }

        void DAGBuilder::visit(ME::RetInst& inst, SelectionDAG& dag)
        {
            std::vector<SDValue> ops;

            // 结合此处考虑 currentChain_ 的作用是什么
            ops.push_back(currentChain_);

            if (inst.res == nullptr)
            {
                dag.getNode(static_cast<unsigned>(ISD::RET), {}, ops);
                return;
            }

            if (inst.res->getType() == ME::OperandType::IMMEI32)
            {
                auto v = dag.getNode(static_cast<unsigned>(ISD::CONST_I32), {I32}, {});
                v.getNode()->setImmI64(static_cast<ME::ImmeI32Operand*>(inst.res)->value);
                ops.push_back(v);
            }
            else if (inst.res->getType() == ME::OperandType::IMMEF32)
            {
                auto v = dag.getNode(static_cast<unsigned>(ISD::CONST_F32), {F32}, {});
                v.getNode()->setImmF32(static_cast<ME::ImmeF32Operand*>(inst.res)->value);
                ops.push_back(v);
            }
            else if (inst.res->getType() == ME::OperandType::REG)
            {
                auto v = getValue(inst.res, dag, mapType(inst.rt));
                ops.push_back(v);
            }
            else
                ERROR("Unsupported return operand type in DAGBuilder");

            dag.getNode(static_cast<unsigned>(ISD::RET), {}, ops);
        }

        void DAGBuilder::visit(ME::LoadInst& inst, SelectionDAG& dag)
        {
            auto    vt  = mapType(inst.dt);
            SDValue ptr = getValue(inst.ptr, dag, BE::PTR);
            // LOAD: (Chain, Address) -> (Value, Chain)
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::LOAD), {vt, BE::TOKEN}, {currentChain_, ptr});
            setDef(inst.res, SDValue(node.getNode(), 0));  // Value is result #0
            currentChain_ = SDValue(node.getNode(), 1);    // Chain is result #1
        }

        void DAGBuilder::visit(ME::StoreInst& inst, SelectionDAG& dag)
        {
            // 生成 STORE 节点并维护 Chain
            SDValue val = getValue(inst.val, dag, mapType(inst.dt));
            SDValue ptr = getValue(inst.ptr, dag, BE::PTR);
            // STORE: (Chain, Value, Address) -> (Chain)
            SDValue storeNode = dag.getNode(static_cast<unsigned>(ISD::STORE), {BE::TOKEN}, {currentChain_, val, ptr});
            currentChain_ = storeNode;
        }

        void DAGBuilder::visit(ME::ArithmeticInst& inst, SelectionDAG& dag)
        {
            bool     f    = isFloatType(inst.dt);
            auto     vt   = mapType(inst.dt);
            SDValue  lhs  = getValue(inst.lhs, dag, vt);
            SDValue  rhs  = getValue(inst.rhs, dag, vt);
            uint32_t opc  = mapArithmeticOpcode(inst.opcode, f);
            SDValue  node = dag.getNode(opc, {vt}, {lhs, rhs});
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

        void DAGBuilder::visit(ME::AllocaInst& inst, SelectionDAG& dag)
        {
            size_t dest_id = static_cast<ME::RegOperand*>(inst.res)->getRegNum();

            DataType* ptr_ty = BE::I64;
            SDValue   v      = dag.getFrameIndexNode(static_cast<int>(dest_id), ptr_ty);

            v.getNode()->setIRRegId(dest_id);

            reg_value_map_[dest_id] = v;
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
            auto vt = mapType(inst.dt);
            std::vector<SDValue> ops;
            
            for (auto& [labelOp, valOp] : inst.incomingVals)
            {
                SDValue val = getValue(valOp, dag, vt);
                SDValue label = getValue(labelOp, dag, nullptr);
                // SelectionDAG PHI operands are [value, label] pairs
                ops.push_back(val);
                ops.push_back(label);
            }
            
            SDValue node = dag.getNode(static_cast<unsigned>(ISD::PHI), {vt}, ops);
            setDef(inst.res, node);
        }

    }  // namespace DAG
}  // namespace BE
