#include <backend/targets/riscv64/isel/rv64_dag_isel.h>
#include <backend/target/target.h>
#include <backend/dag/selection_dag.h>
#include <backend/dag/isd.h>
#include <backend/dag/dag_builder.h>
#include <backend/targets/riscv64/dag/rv64_dag_legalize.h>
#include <backend/targets/riscv64/rv64_defs.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_function.h>
#include <debug.h>
#include <transfer.h>
#include <queue>
#include <functional>

namespace BE::RV64
{
    [[maybe_unused]]static inline bool imm12(int imm) { return imm >= -2048 && imm <= 2047; }

    static Operator getLoadOpForType(BE::DataType* dt)
    {
        if (dt && dt->dt == BE::DataType::Type::FLOAT)
            return (dt->dl == BE::DataType::Length::B32) ? Operator::FLW : Operator::FLD;
        return (dt && dt->dl == BE::DataType::Length::B32) ? Operator::LW : Operator::LD;
    }

    static Operator getStoreOpForType(BE::DataType* dt)
    {
        if (dt && dt->dt == BE::DataType::Type::FLOAT)
            return (dt->dl == BE::DataType::Length::B32) ? Operator::FSW : Operator::FSD;
        return (dt && dt->dl == BE::DataType::Length::B32) ? Operator::SW : Operator::SD;
    }

    std::vector<const DAG::SDNode*> DAGIsel::scheduleDAG(const DAG::SelectionDAG& dag)
    {
        std::vector<const DAG::SDNode*> result;
        std::set<const DAG::SDNode*> visited;

        // 后序遍历辅助函数
        std::function<void(const DAG::SDNode*)> postOrder = [&](const DAG::SDNode* node) {
            if (!node || visited.count(node)) return;
            visited.insert(node);

            // 先访问所有操作数（依赖）
            for (unsigned i = 0; i < node->getNumOperands(); ++i)
            {
                const DAG::SDNode* opNode = node->getOperand(i).getNode();
                if (opNode) postOrder(opNode);
            }

            // 后访问当前节点
            result.push_back(node);
        };

        // 从所有节点开始遍历（确保不遗漏任何节点）
        for (const auto* node : dag.getNodes())
        {
            if (node && !visited.count(node))
                postOrder(node);
        }

        return result;
    }

    void DAGIsel::allocateRegistersForNode(const DAG::SDNode* node)
    {
        // ============================================================================
        // 为调度后的 DAG 节点预分配虚拟寄存器
        // ============================================================================
        //
        // 为什么需要：
        // - 在指令选择前，先为每个计算结果分配虚拟寄存器
        // - 确保跨基本块的值（如 PHI 操作数）使用一致的寄存器映射
        // - 分离"分配"与"使用"两个阶段，简化后续的指令生成逻辑
        //
        // 策略：
        // - 若节点对应 IR 寄存器，复用 IR 寄存器映射
        // - 否则分配临时虚拟寄存器
        // - 常量、LABEL、SYMBOL 等叶子节点按需实例化，无需预分配

        if (node->getNumValues() == 0) return;

        auto opcode = static_cast<DAG::ISD>(node->getOpcode());

        if (opcode == DAG::ISD::LABEL || opcode == DAG::ISD::SYMBOL || opcode == DAG::ISD::CONST_I32 ||
            opcode == DAG::ISD::CONST_I64 || opcode == DAG::ISD::CONST_F32 || opcode == DAG::ISD::FRAME_INDEX)
            return;

        BE::DataType* dt = node->getValueType(0);
        Register      vreg;

        if (node->hasIRRegId())
            vreg = getOrCreateVReg(node->getIRRegId(), dt);
        else
            vreg = getVReg(dt);

        nodeToVReg_[node] = vreg;
    }

    Register DAGIsel::getOperandReg(const DAG::SDNode* node, BE::Block* m_block)
    {
        // ============================================================================
        // 操作数获取：统一的实例化入口
        // ============================================================================
        //
        // 作用：为 DAG 节点返回或实例化对应的寄存器
        //
        // 为什么需要：
        // - 指令选择时，需要将 DAG 节点（抽象）映射到寄存器（具体）
        // - 常量、地址节点在首次使用时才生成指令，这可以避免为未使用的常量生成无用指令
        // - 统一处理已分配寄存器、IR 寄存器映射、按需实例化三种情况

        if (!node) ERROR("Cannot get register for null node");

        auto opcode = static_cast<DAG::ISD>(node->getOpcode());

        auto it = nodeToVReg_.find(node);
        if (it != nodeToVReg_.end()) return it->second;

        if (opcode == DAG::ISD::REG && node->hasIRRegId())
            return getOrCreateVReg(node->getIRRegId(), node->getNumValues() > 0 ? node->getValueType(0) : BE::I64);

        if (opcode == DAG::ISD::CONST_I32 || opcode == DAG::ISD::CONST_I64)
        {
            DataType* dt      = (opcode == DAG::ISD::CONST_I32) ? BE::I32 : BE::I64;
            Register  destReg = getVReg(dt);
            int64_t   imm     = node->hasImmI64() ? node->getImmI64() : 0;

            m_block->insts.push_back(createMove(new RegOperand(destReg), static_cast<int>(imm), LOC_STR));

            nodeToVReg_[node] = destReg;
            return destReg;
        }

        if (opcode == DAG::ISD::CONST_F32)
        {
            Register destReg = getVReg(BE::F32);

            if (node->hasImmF32())
            {
                float    f_val = node->getImmF32();
                uint32_t bits;
                memcpy(&bits, &f_val, sizeof(float));
                Register tempReg = getVReg(BE::I32);
                m_block->insts.push_back(createMove(new RegOperand(tempReg), static_cast<int>(bits), LOC_STR));
                m_block->insts.push_back(createR2Inst(Operator::FMV_W_X, destReg, tempReg));
            }

            nodeToVReg_[node] = destReg;
            return destReg;
        }

        if (opcode == DAG::ISD::FRAME_INDEX || opcode == DAG::ISD::SYMBOL) return materializeAddress(node, m_block);

        ERROR("Node not scheduled or cannot be materialized: %s",
            DAG::toString(static_cast<DAG::ISD>(node->getOpcode())));
        return Register();
    }

    Register DAGIsel::materializeAddress(const DAG::SDNode* node, BE::Block* m_block)
    {
        // ============================================================================
        // 地址实例化：使用者负责实例化
        // ============================================================================
        //
        // 作用：将地址节点（FRAME_INDEX/SYMBOL）实例化为寄存器
        //
        // 为什么需要：
        // - 地址节点本身不生成独立的指令，而是由使用者（LOAD/STORE/ADD）决定如何实例化
        // - FRAME_INDEX 需要生成抽象的 FrameIndexOperand，由后续 Pass 替换为实际偏移
        // - SYMBOL 需要生成 LA 伪指令加载全局变量地址

        if (!node) ERROR("Cannot materialize null address");

        auto opcode = static_cast<DAG::ISD>(node->getOpcode());

        if (opcode == DAG::ISD::FRAME_INDEX && node->hasIRRegId())
        {
            size_t   ir_reg_id = node->getIRRegId();
            Register addrReg   = getVReg(BE::I64);

            Instr* addr_inst = createIInst(Operator::ADDI, addrReg, PR::sp, new FrameIndexOperand(ir_reg_id));
            m_block->insts.push_back(addr_inst);

            return addrReg;
        }

        if (opcode == DAG::ISD::SYMBOL && node->hasSymbol())
        {
            Register addrReg = getVReg(BE::I64);
            Label    symbolLabel(node->getSymbol(), false, true);
            m_block->insts.push_back(createUInst(Operator::LA, addrReg, symbolLabel));
            return addrReg;
        }

        auto it = nodeToVReg_.find(node);
        if (it != nodeToVReg_.end()) return it->second;

        if (opcode == DAG::ISD::REG && node->hasIRRegId())
            return getOrCreateVReg(node->getIRRegId(), node->getNumValues() > 0 ? node->getValueType(0) : BE::I64);

        ERROR("Cannot materialize address for opcode: %s", DAG::toString(opcode));
    }

    int DAGIsel::dataTypeSize(BE::DataType* dt)
    {
        if (dt == BE::I32 || dt == BE::F32) return 4;
        if (dt == BE::I64 || dt == BE::F64 || dt == BE::PTR) return 8;
        return 4;
    }

    Register DAGIsel::getOrCreateVReg(size_t ir_reg_id, BE::DataType* dt)
    {
        auto it = ctx_.vregMap.find(ir_reg_id);
        if (it != ctx_.vregMap.end())
        {
            if (it->second.dt == dt) return it->second;

            // 如果进入到这个分支，说明类型不匹配
            // 这个问题在 ARM 中需要插入类型转化
            // 但 RISC-V 中对寄存器宽度没有要求，所以这里直接返回也可以
            return it->second;
        }

        Register vreg           = getVReg(dt);
        ctx_.vregMap[ir_reg_id] = vreg;
        return vreg;
    }

    void DAGIsel::importGlobals()
    {
        // 遍历 IR 模块中的所有全局变量
        for (auto* glb : ir_module_->globalVars)
        {
            // 转换类型: ME::DataType -> BE::DataType
            BE::DataType* beType = BE::I32;  // 默认 I32
            if (glb->dt == ME::DataType::F32)
                beType = BE::F32;
            else if (glb->dt == ME::DataType::I64 || glb->dt == ME::DataType::PTR)
                beType = BE::I64;

            // 创建后端全局变量对象
            auto* gv = new BE::GlobalVariable(beType, glb->name);

            // 处理数组维度
            gv->dims = glb->initList.arrayDims;

            // 处理初始化值
            if (glb->init)
            {
                // 标量初始化
                if (auto* immI32 = dynamic_cast<ME::ImmeI32Operand*>(glb->init))
                    gv->initVals.push_back(immI32->value);
                else if (auto* immF32 = dynamic_cast<ME::ImmeF32Operand*>(glb->init))
                    gv->initVals.push_back(FLOAT_TO_INT_BITS(immF32->value));
            }
            else if (!glb->initList.initList.empty())
            {
                // 数组初始化列表
                for (const auto& val : glb->initList.initList)
                {
                    if (val.type == FE::AST::floatType)
                        gv->initVals.push_back(FLOAT_TO_INT_BITS(val.floatValue));
                    else
                        gv->initVals.push_back(val.intValue);
                }
            }

            m_backend_module->globals.push_back(gv);
        }
    }

    void DAGIsel::collectAllocas(ME::Function* ir_func)
    {
        // 遍历函数所有基本块中的指令
        for (auto& [blockId, block] : ir_func->blocks)
        {
            for (auto* inst : block->insts)
            {
                // 查找 AllocaInst
                if (auto* alloca = dynamic_cast<ME::AllocaInst*>(inst))
                {
                    // 获取结果寄存器的 ID
                    auto* resReg = dynamic_cast<ME::RegOperand*>(alloca->res);
                    if (!resReg) continue;

                    size_t irRegId = resReg->regNum;

                    // 计算分配大小
                    int elemSize = (alloca->dt == ME::DataType::F32 || alloca->dt == ME::DataType::I32) ? 4 : 8;
                    int totalSize = elemSize;

                    // 数组：计算总大小
                    for (int dim : alloca->dims) totalSize *= dim;

                    // 注册到 frameInfo
                    ctx_.mfunc->frameInfo.createLocalObject(irRegId, totalSize, 16);

                    // 记录 alloca 到栈帧索引的映射
                    ctx_.allocaFI[irRegId] = static_cast<int>(irRegId);
                }
            }
        }
    }

    void DAGIsel::setupParameters(ME::Function* ir_func)
    {
        // 入口块（假设 blockId 最小的为入口）
        BE::Block* entry = nullptr;
        if (!ctx_.mfunc->blocks.empty()) entry = ctx_.mfunc->blocks.begin()->second;
        if (!entry) return;

        const Register iArgRegs[] = {PR::a0, PR::a1, PR::a2, PR::a3, PR::a4, PR::a5, PR::a6, PR::a7};
        const Register fArgRegs[] = {PR::fa0, PR::fa1, PR::fa2, PR::fa3, PR::fa4, PR::fa5, PR::fa6, PR::fa7};

        size_t argIdx      = 0;
        int    stackOffset = 0;

        // 按参数顺序建立 vreg 映射并生成加载指令
        for (const auto& [argType, argOp] : ir_func->funcDef->argRegs)
        {
            auto* regOp = dynamic_cast<ME::RegOperand*>(argOp);
            if (!regOp) continue;

            BE::DataType* beType = BE::I64;  // 默认 I64
            if (argType == ME::DataType::F32)
                beType = BE::F32;
            else if (argType == ME::DataType::I32)
                beType = BE::I32;

            Register vreg = getOrCreateVReg(regOp->regNum, beType);
            ctx_.mfunc->params.push_back(vreg);

            if (argIdx < 8)
            {
                Register srcReg = (beType == BE::F32 || beType == BE::F64) ? fArgRegs[argIdx] : iArgRegs[argIdx];
                entry->insts.push_back(createMove(new RegOperand(vreg), new RegOperand(srcReg), "param_reg"));
            }
            else
            {
                int      off = static_cast<int>((argIdx - 8) * 8);
                Operator op  = getLoadOpForType(beType);
                auto*    ld  = createIInst(op, vreg, PR::sp, off);
                ld->comment = "param_stack";
                entry->insts.push_back(ld);
                stackOffset = off + 8;
            }

            ++argIdx;
        }

        if (stackOffset > 0) ctx_.mfunc->hasStackParam = true;
    }

    void DAGIsel::selectCopy(const DAG::SDNode* node, BE::Block* m_block)
    {
        if (node->getNumOperands() < 1) return;

        const DAG::SDNode* src = node->getOperand(0).getNode();
        if (!src) return;

        Register dst    = getOperandReg(node, m_block);
        Register srcReg = getOperandReg(src, m_block);

        m_block->insts.push_back(createMove(new RegOperand(dst), new RegOperand(srcReg), LOC_STR));
    }

    void DAGIsel::selectPhi(const DAG::SDNode* node, BE::Block* m_block)
    {
        // PHI 节点的操作数成对出现：[value0, label0, value1, label1, ...]
        unsigned numOps = node->getNumOperands();
        if (numOps < 2 || numOps % 2 != 0) return;

        // 获取目标寄存器
        Register dst = nodeToVReg_.at(node);

        // 创建 PhiInst
        auto* phi = new PhiInst(dst);

        // 遍历操作数对
        for (unsigned i = 0; i < numOps; i += 2)
        {
            const DAG::SDNode* valNode   = node->getOperand(i).getNode();
            const DAG::SDNode* labelNode = node->getOperand(i + 1).getNode();

            if (!valNode || !labelNode) continue;

            // 获取前驱块标签
            uint32_t predLabel = 0;
            if (labelNode->hasImmI64())
                predLabel = static_cast<uint32_t>(labelNode->getImmI64());

            // 获取值：对于常量直接使用立即数，避免在当前块实例化
            Operand* srcOp = nullptr;
            auto     valOp = static_cast<DAG::ISD>(valNode->getOpcode());

            if (valOp == DAG::ISD::CONST_I32 || valOp == DAG::ISD::CONST_I64)
            {
                int imm = valNode->hasImmI64() ? static_cast<int>(valNode->getImmI64()) : 0;
                srcOp = new I32Operand(imm);
            }
            else if (valOp == DAG::ISD::CONST_F32)
            {
                float f = valNode->hasImmF32() ? valNode->getImmF32() : 0.0f;
                srcOp = new F32Operand(f);
            }
            else
            {
                // 寄存器：使用 getOrCreateVReg 而非 getOperandReg（避免在当前块实例化）
                if (valNode->hasIRRegId())
                {
                    DataType* dt = valNode->getNumValues() > 0 ? valNode->getValueType(0) : BE::I64;
                    Register reg = getOrCreateVReg(valNode->getIRRegId(), dt);
                    srcOp = new RegOperand(reg);
                }
                else
                {
                    // 已调度节点，从 nodeToVReg_ 获取
                    auto it = nodeToVReg_.find(valNode);
                    if (it != nodeToVReg_.end())
                        srcOp = new RegOperand(it->second);
                    else
                        continue;
                }
            }

            phi->incomingVals[predLabel] = srcOp;
        }

        m_block->insts.push_back(phi);
    }

    void DAGIsel::selectBinary(const DAG::SDNode* node, BE::Block* m_block)
    {
        if (node->getNumOperands() < 2) return;

        auto opcode = static_cast<DAG::ISD>(node->getOpcode());

        Register dst = nodeToVReg_.at(node);

        const DAG::SDNode* lhs = node->getOperand(0).getNode();
        const DAG::SDNode* rhs = node->getOperand(1).getNode();

        Register lhsReg;
        auto     lhsOp = static_cast<DAG::ISD>(lhs->getOpcode());

        bool isAllocaReg = false;
        int  allocaFI    = -1;
        if (lhsOp == DAG::ISD::REG && lhs->hasIRRegId())
        {
            auto it = ctx_.allocaFI.find(lhs->getIRRegId());
            if (it != ctx_.allocaFI.end())
            {
                isAllocaReg = true;
                allocaFI    = it->second;
            }
        }

        if (lhsOp == DAG::ISD::SYMBOL)
            lhsReg = materializeAddress(lhs, m_block);
        else if (lhsOp == DAG::ISD::FRAME_INDEX || isAllocaReg)
        {
            lhsReg            = getVReg(BE::I64);
            int    fi         = isAllocaReg ? allocaFI : lhs->getFrameIndex();
            Instr* addrInst   = createIInst(Operator::ADDI, lhsReg, PR::sp, 0);
            addrInst->fiop    = new FrameIndexOperand(fi);
            addrInst->use_ops = true;
            m_block->insts.push_back(addrInst);
        }
        else
            lhsReg = getOperandReg(lhs, m_block);

        Register rhsReg;
        int      rhsImm     = 0;
        bool     isRhsConst = false;

        auto rhsOp = static_cast<DAG::ISD>(rhs->getOpcode());
        if (rhsOp == DAG::ISD::CONST_I32 && rhs->hasImmI64())
        {
            rhsImm     = static_cast<int>(rhs->getImmI64());
            isRhsConst = true;
        }
        else
            rhsReg = getOperandReg(rhs, m_block);

        Operator op;
        bool     isFloat =
            (node->getNumValues() > 0 && (node->getValueType(0) == BE::F32 || node->getValueType(0) == BE::F64));
        bool is32bit = (dst.dt == BE::I32);

        switch (opcode)
        {
            case DAG::ISD::ADD: op = isFloat ? Operator::FADD_S : (is32bit ? Operator::ADDW : Operator::ADD); break;
            case DAG::ISD::SUB: op = isFloat ? Operator::FSUB_S : (is32bit ? Operator::SUBW : Operator::SUB); break;
            case DAG::ISD::MUL: op = isFloat ? Operator::FMUL_S : (is32bit ? Operator::MULW : Operator::MUL); break;
            case DAG::ISD::DIV: op = isFloat ? Operator::FDIV_S : (is32bit ? Operator::DIVW : Operator::DIV); break;
            case DAG::ISD::FADD: op = Operator::FADD_S; break;
            case DAG::ISD::FSUB: op = Operator::FSUB_S; break;
            case DAG::ISD::FMUL: op = Operator::FMUL_S; break;
            case DAG::ISD::FDIV: op = Operator::FDIV_S; break;
            case DAG::ISD::MOD: op = is32bit ? Operator::REMW : Operator::REM; break;
            case DAG::ISD::AND: op = Operator::AND; break;
            case DAG::ISD::OR: op = Operator::OR; break;
            case DAG::ISD::XOR: op = Operator::XOR; break;
            case DAG::ISD::SHL: op = Operator::SLL; break;
            case DAG::ISD::ASHR: op = Operator::SRA; break;
            case DAG::ISD::LSHR: op = Operator::SRL; break;
            default: ERROR("Unsupported binary operator: %d", node->getOpcode()); return;
        }

        if (isRhsConst)
        {
            bool     is32bit = (dst.dt == BE::I32);
            Operator iop;
            bool     hasImmForm = true;

            switch (op)
            {
                case Operator::ADD: iop = is32bit ? Operator::ADDIW : Operator::ADDI; break;
                case Operator::AND: iop = Operator::ANDI; break;
                case Operator::OR: iop = Operator::ORI; break;
                case Operator::XOR: iop = Operator::XORI; break;
                case Operator::SLL: iop = is32bit ? Operator::SLLIW : Operator::SLLI; break;
                case Operator::SRA: iop = is32bit ? Operator::SRAIW : Operator::SRAI; break;
                case Operator::SRL: iop = is32bit ? Operator::SRLIW : Operator::SRLI; break;
                default: hasImmForm = false; break;
            }

            if (hasImmForm)
                m_block->insts.push_back(createIInst(iop, dst, lhsReg, rhsImm));
            else
            {
                Register tmpReg = getVReg(lhsReg.dt);
                m_block->insts.push_back(createMove(new RegOperand(tmpReg), rhsImm, LOC_STR));
                m_block->insts.push_back(createRInst(op, dst, lhsReg, tmpReg));
            }
        }
        else
            m_block->insts.push_back(createRInst(op, dst, lhsReg, rhsReg));
    }

    void DAGIsel::selectUnary(const DAG::SDNode* node, BE::Block* m_block)
    {
        // ============================================================================
        // TODO: 选择一元运算节点（可选）
        // ============================================================================
        //
        // 说明：
        // 一元运算（如 NEG、NOT）在基础 DAG 中较少使用。
        // 若你的 IR 或优化中产生了一元节点，可在此处理。
        //
        TODO("可选：处理一元运算（NEG/NOT 等）");
    }

    bool DAGIsel::selectAddress(const DAG::SDNode* addrNode, const DAG::SDNode*& baseNode, int64_t& offset)
    {
        if (!addrNode) return false;

        auto opcode = static_cast<DAG::ISD>(addrNode->getOpcode());

        if (opcode == DAG::ISD::FRAME_INDEX || opcode == DAG::ISD::SYMBOL)
        {
            baseNode = addrNode;
            offset   = 0;
            return true;
        }

        if (opcode == DAG::ISD::ADD)
        {
            const DAG::SDNode* lhs = addrNode->getOperand(0).getNode();
            const DAG::SDNode* rhs = addrNode->getOperand(1).getNode();

            const DAG::SDNode* lhsBase;
            int64_t            lhsOffset = 0;
            if (selectAddress(lhs, lhsBase, lhsOffset))
            {
                if ((static_cast<DAG::ISD>(rhs->getOpcode()) == DAG::ISD::CONST_I32 ||
                        static_cast<DAG::ISD>(rhs->getOpcode()) == DAG::ISD::CONST_I64) &&
                    rhs->hasImmI64())
                {
                    baseNode = lhsBase;
                    offset   = lhsOffset + rhs->getImmI64();
                    return true;
                }
                return false;
            }

            const DAG::SDNode* rhsBase;
            int64_t            rhsOffset = 0;
            if (selectAddress(rhs, rhsBase, rhsOffset))
            {
                if ((static_cast<DAG::ISD>(lhs->getOpcode()) == DAG::ISD::CONST_I32 ||
                        static_cast<DAG::ISD>(lhs->getOpcode()) == DAG::ISD::CONST_I64) &&
                    lhs->hasImmI64())
                {
                    baseNode = rhsBase;
                    offset   = rhsOffset + lhs->getImmI64();
                    return true;
                }
                return false;
            }

            return false;
        }

        return false;
    }

    void DAGIsel::selectLoad(const DAG::SDNode* node, BE::Block* m_block)
    {
        // ============================================================================
        // 选择 LOAD 节点, 作为示例实现，仅供参考
        // ============================================================================
        //
        // 作用：
        // 为 LOAD 节点生成目标相关的访存指令（LW/LD/FLW/FLD）。
        //
        // 为什么需要地址选择：
        // - 地址可能是简单的 [base + offset]，也可能是复杂表达式
        // - 声明式地址选择（selectAddress）可将常见模式折叠到访存指令的立即数字段
        // - 若地址过于复杂，需先完整计算到寄存器

        if (node->getNumOperands() < 2) return;

        Register           dst  = nodeToVReg_.at(node);
        const DAG::SDNode* addr = node->getOperand(1).getNode();

        const DAG::SDNode* baseNode;
        int64_t            offset = 0;

        if (selectAddress(addr, baseNode, offset))
        {
            Register baseReg;

            if (static_cast<DAG::ISD>(baseNode->getOpcode()) == DAG::ISD::FRAME_INDEX)
            {
                int fi            = baseNode->getFrameIndex();
                baseReg           = PR::sp;
                Operator loadOp   = getLoadOpForType(dst.dt);
                auto*    ldInst   = createIInst(loadOp, dst, baseReg, 0);
                ldInst->imme      = static_cast<int>(offset);
                ldInst->fiop      = new FrameIndexOperand(fi);
                ldInst->use_ops   = true;
                m_block->insts.push_back(ldInst);
                return;
            }
            else if (static_cast<DAG::ISD>(baseNode->getOpcode()) == DAG::ISD::SYMBOL && baseNode->hasSymbol())
            {
                std::string symbol = baseNode->getSymbol();
                baseReg            = getVReg(BE::I64);
                Label symbolLabel(symbol, false, true);
                m_block->insts.push_back(createUInst(Operator::LA, baseReg, symbolLabel));
            }
            else
                baseReg = getOperandReg(baseNode, m_block);

            Operator loadOp = getLoadOpForType(dst.dt);

            if (offset < -2048 || offset > 2047)
            {
                Register offsetReg = getVReg(BE::I64);
                m_block->insts.push_back(createMove(new RegOperand(offsetReg), static_cast<int>(offset), LOC_STR));
                Register finalBase = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::ADD, finalBase, baseReg, offsetReg));
                m_block->insts.push_back(createIInst(loadOp, dst, finalBase, 0));
            }
            else
                m_block->insts.push_back(createIInst(loadOp, dst, baseReg, static_cast<int>(offset)));
        }
        else
        {
            Register addrReg = getOperandReg(addr, m_block);
            Operator loadOp  = getLoadOpForType(dst.dt);
            m_block->insts.push_back(createIInst(loadOp, dst, addrReg, 0));
        }
    }

    void DAGIsel::selectStore(const DAG::SDNode* node, BE::Block* m_block)
    {
        // STORE 操作数：[Chain, Value, Address]
        if (node->getNumOperands() < 3) return;

        const DAG::SDNode* valNode  = node->getOperand(1).getNode();
        const DAG::SDNode* addrNode = node->getOperand(2).getNode();

        if (!valNode || !addrNode) return;

        // 获取要存储的值的寄存器
        Register srcReg = getOperandReg(valNode, m_block);

        // 确定存储指令类型
        DataType* valType = valNode->getNumValues() > 0 ? valNode->getValueType(0) : BE::I32;
        Operator  storeOp = getStoreOpForType(valType);

        // 尝试地址选择
        const DAG::SDNode* baseNode;
        int64_t            offset = 0;

        if (selectAddress(addrNode, baseNode, offset))
        {
            Register baseReg;

            if (static_cast<DAG::ISD>(baseNode->getOpcode()) == DAG::ISD::FRAME_INDEX)
            {
                int fi            = baseNode->getFrameIndex();
                baseReg           = PR::sp;
                auto* storeInst   = createSInst(storeOp, srcReg, baseReg, 0);
                storeInst->imme   = static_cast<int>(offset);
                storeInst->fiop   = new FrameIndexOperand(fi);
                storeInst->use_ops = true;
                m_block->insts.push_back(storeInst);
                return;
            }
            else if (static_cast<DAG::ISD>(baseNode->getOpcode()) == DAG::ISD::SYMBOL && baseNode->hasSymbol())
            {
                std::string symbol = baseNode->getSymbol();
                baseReg            = getVReg(BE::I64);
                Label symbolLabel(symbol, false, true);
                m_block->insts.push_back(createUInst(Operator::LA, baseReg, symbolLabel));
            }
            else
                baseReg = getOperandReg(baseNode, m_block);

            // 检查立即数范围
            if (offset < -2048 || offset > 2047)
            {
                Register offsetReg = getVReg(BE::I64);
                m_block->insts.push_back(createMove(new RegOperand(offsetReg), static_cast<int>(offset), LOC_STR));
                Register finalBase = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::ADD, finalBase, baseReg, offsetReg));
                m_block->insts.push_back(createSInst(storeOp, srcReg, finalBase, 0));
            }
            else
                m_block->insts.push_back(createSInst(storeOp, srcReg, baseReg, static_cast<int>(offset)));
        }
        else
        {
            Register addrReg = getOperandReg(addrNode, m_block);
            m_block->insts.push_back(createSInst(storeOp, srcReg, addrReg, 0));
        }
    }

    void DAGIsel::selectICmp(const DAG::SDNode* node, BE::Block* m_block)
    {
        if (node->getNumOperands() < 2) return;

        Register dst = nodeToVReg_.at(node);

        const DAG::SDNode* lhs = node->getOperand(0).getNode();
        const DAG::SDNode* rhs = node->getOperand(1).getNode();

        Register lhsReg = getOperandReg(lhs, m_block);
        Register rhsReg = getOperandReg(rhs, m_block);

        // 从节点的立即数中获取比较条件码
        int condCode = node->hasImmI64() ? static_cast<int>(node->getImmI64()) : 0;
        auto cond = static_cast<ME::ICmpOp>(condCode);

        switch (cond)
        {
            case ME::ICmpOp::EQ:
            {
                // XOR + SEQZ: dst = (lhs ^ rhs) == 0
                Register tmp = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::XOR, tmp, lhsReg, rhsReg));
                m_block->insts.push_back(createIInst(Operator::SLTIU, dst, tmp, 1));
                break;
            }
            case ME::ICmpOp::NE:
            {
                // XOR + SNEZ: dst = (lhs ^ rhs) != 0
                Register tmp = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::XOR, tmp, lhsReg, rhsReg));
                m_block->insts.push_back(createRInst(Operator::SLTU, dst, PR::x0, tmp));
                break;
            }
            case ME::ICmpOp::SLT:
                m_block->insts.push_back(createRInst(Operator::SLT, dst, lhsReg, rhsReg));
                break;
            case ME::ICmpOp::SGE:
            {
                // SGE = NOT(SLT)
                Register tmp = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::SLT, tmp, lhsReg, rhsReg));
                m_block->insts.push_back(createIInst(Operator::XORI, dst, tmp, 1));
                break;
            }
            case ME::ICmpOp::SGT:
                // SGT = SLT with swapped operands
                m_block->insts.push_back(createRInst(Operator::SLT, dst, rhsReg, lhsReg));
                break;
            case ME::ICmpOp::SLE:
            {
                // SLE = NOT(SGT) = NOT(SLT swapped)
                Register tmp = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::SLT, tmp, rhsReg, lhsReg));
                m_block->insts.push_back(createIInst(Operator::XORI, dst, tmp, 1));
                break;
            }
            case ME::ICmpOp::ULT:
                m_block->insts.push_back(createRInst(Operator::SLTU, dst, lhsReg, rhsReg));
                break;
            case ME::ICmpOp::UGE:
            {
                Register tmp = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::SLTU, tmp, lhsReg, rhsReg));
                m_block->insts.push_back(createIInst(Operator::XORI, dst, tmp, 1));
                break;
            }
            case ME::ICmpOp::UGT:
                m_block->insts.push_back(createRInst(Operator::SLTU, dst, rhsReg, lhsReg));
                break;
            case ME::ICmpOp::ULE:
            {
                Register tmp = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::SLTU, tmp, rhsReg, lhsReg));
                m_block->insts.push_back(createIInst(Operator::XORI, dst, tmp, 1));
                break;
            }
            default:
                ERROR("Unsupported ICMP condition: %d", condCode);
        }
    }

    void DAGIsel::selectFCmp(const DAG::SDNode* node, BE::Block* m_block)
    {
        if (node->getNumOperands() < 2) return;

        Register dst = nodeToVReg_.at(node);

        const DAG::SDNode* lhs = node->getOperand(0).getNode();
        const DAG::SDNode* rhs = node->getOperand(1).getNode();

        Register lhsReg = getOperandReg(lhs, m_block);
        Register rhsReg = getOperandReg(rhs, m_block);

        // 从节点的立即数中获取比较条件码
        int condCode = node->hasImmI64() ? static_cast<int>(node->getImmI64()) : 0;
        auto cond = static_cast<ME::FCmpOp>(condCode);

        switch (cond)
        {
            case ME::FCmpOp::OEQ:
            case ME::FCmpOp::UEQ:
                m_block->insts.push_back(createRInst(Operator::FEQ_S, dst, lhsReg, rhsReg));
                break;
            case ME::FCmpOp::OLT:
            case ME::FCmpOp::ULT:
                m_block->insts.push_back(createRInst(Operator::FLT_S, dst, lhsReg, rhsReg));
                break;
            case ME::FCmpOp::OLE:
            case ME::FCmpOp::ULE:
                m_block->insts.push_back(createRInst(Operator::FLE_S, dst, lhsReg, rhsReg));
                break;
            case ME::FCmpOp::OGT:
            case ME::FCmpOp::UGT:
                // OGT = OLT with swapped operands
                m_block->insts.push_back(createRInst(Operator::FLT_S, dst, rhsReg, lhsReg));
                break;
            case ME::FCmpOp::OGE:
            case ME::FCmpOp::UGE:
                // OGE = OLE with swapped operands
                m_block->insts.push_back(createRInst(Operator::FLE_S, dst, rhsReg, lhsReg));
                break;
            case ME::FCmpOp::ONE:
            case ME::FCmpOp::UNE:
            {
                // ONE = NOT(OEQ)
                Register tmp = getVReg(BE::I64);
                m_block->insts.push_back(createRInst(Operator::FEQ_S, tmp, lhsReg, rhsReg));
                m_block->insts.push_back(createIInst(Operator::XORI, dst, tmp, 1));
                break;
            }
            default:
                ERROR("Unsupported FCMP condition: %d", condCode);
        }
    }

    void DAGIsel::selectBranch(const DAG::SDNode* node, BE::Block* m_block)
    {
        auto opcode = static_cast<DAG::ISD>(node->getOpcode());

        if (opcode == DAG::ISD::BR)
        {
            // 无条件分支: BR [Chain, Target]
            if (node->getNumOperands() < 1) return;

            // allow optional chain at operand 0
            int                  targetIdx = (node->getNumOperands() == 1) ? 0 : 1;
            const DAG::SDNode*   targetNode = node->getOperand(targetIdx).getNode();
            if (!targetNode || !targetNode->hasImmI64()) return;

            int targetLabel = static_cast<int>(targetNode->getImmI64());
            m_block->insts.push_back(createJInst(Operator::JAL, PR::x0, Label(targetLabel)));
        }
        else if (opcode == DAG::ISD::BRCOND)
        {
            // 条件分支: BRCOND [Chain, Cond, TrueLabel, FalseLabel]
            if (node->getNumOperands() < 3) return;

            int condIdx = (node->getNumOperands() == 3) ? 0 : 1;
            const DAG::SDNode* condNode      = node->getOperand(condIdx).getNode();
            const DAG::SDNode* trueLabelNode = node->getOperand(condIdx + 1).getNode();
            const DAG::SDNode* falseLabelNode = node->getOperand(condIdx + 2).getNode();

            if (!condNode || !trueLabelNode || !falseLabelNode) return;

            Register condReg = getOperandReg(condNode, m_block);

            int trueLabel = trueLabelNode->hasImmI64() ? static_cast<int>(trueLabelNode->getImmI64()) : 0;
            int falseLabel = falseLabelNode->hasImmI64() ? static_cast<int>(falseLabelNode->getImmI64()) : 0;

            // 条件非 0 则跳转到 trueLabel
            m_block->insts.push_back(createBInst(Operator::BNE, condReg, PR::x0, Label(trueLabel)));

            // 否则跳转到 falseLabel
            m_block->insts.push_back(createJInst(Operator::JAL, PR::x0, Label(falseLabel)));
        }
    }

    void DAGIsel::selectCall(const DAG::SDNode* node, BE::Block* m_block)
    {
        // CALL 操作数: [Chain, Callee, Arg0, Arg1, ...]
        if (node->getNumOperands() < 2) return;

        const DAG::SDNode* calleeNode = node->getOperand(1).getNode();
        if (!calleeNode) return;

        // 提取函数名
        std::string funcName = calleeNode->hasSymbol() ? calleeNode->getSymbol() : "unknown";

        // 处理内置函数
        if (funcName.find("llvm.memset") != std::string::npos)
            funcName = "memset";
        else if (funcName.find("llvm.memcpy") != std::string::npos)
            funcName = "memcpy";

        const Register iArgRegs[] = {PR::a0, PR::a1, PR::a2, PR::a3, PR::a4, PR::a5, PR::a6, PR::a7};
        const Register fArgRegs[] = {PR::fa0, PR::fa1, PR::fa2, PR::fa3, PR::fa4, PR::fa5, PR::fa6, PR::fa7};

        int argCount      = static_cast<int>(node->getNumOperands()) - 2;
        int stackArgBytes = argCount > 8 ? (argCount - 8) * 8 : 0;
        int tempBase      = stackArgBytes;  // 临时区紧跟在真实栈参数之后

        int iRegCnt = 0;
        int fRegCnt = 0;

        struct ArgInfo
        {
            unsigned  pos;
            Register  reg;
            DataType* type;
        };
        std::vector<ArgInfo> regArgs;

        // 先将所有参数写入栈上的缓冲区，避免后续搬运覆盖尚未使用的源值
        for (unsigned idx = 2; idx < node->getNumOperands(); ++idx)
        {
            const DAG::SDNode* argNode = node->getOperand(idx).getNode();
            if (!argNode) continue;

            Register  argReg  = getOperandReg(argNode, m_block);
            DataType* argType = argNode->getNumValues() > 0 ? argNode->getValueType(0) : BE::I64;

            unsigned argPos = idx - 2;  // argument index in call
            if (argPos < 8)
            {
                regArgs.push_back({argPos, argReg, argType});
                Operator op = getStoreOpForType(argType);
                auto*    st = createSInst(op, argReg, PR::sp, static_cast<int>(tempBase + argPos * 8));
                st->comment = "call_stackarg";
                m_block->insts.push_back(st);
            }
            else
            {
                int      off = static_cast<int>((argPos - 8) * 8);
                Operator op  = getStoreOpForType(argType);
                auto*    st  = createSInst(op, argReg, PR::sp, off);
                st->comment  = "call_stackarg";
                m_block->insts.push_back(st);
            }
        }

        // 再从缓冲区加载到真实的调用寄存器，保证并行搬运正确
        for (auto& info : regArgs)
        {
            Register dst = (info.type == BE::F32 || info.type == BE::F64) ? fArgRegs[info.pos] : iArgRegs[info.pos];
            Operator op  = getLoadOpForType(info.type);
            auto*    ld  = createIInst(op, dst, PR::sp, static_cast<int>(tempBase + info.pos * 8));
            ld->comment  = "call_stackarg";
            m_block->insts.push_back(ld);
            if (info.type == BE::F32 || info.type == BE::F64)
                ++fRegCnt;
            else
                ++iRegCnt;
        }

        // 生成 CALL 指令
        m_block->insts.push_back(createCallInst(Operator::CALL, funcName, iRegCnt, fRegCnt));

        // 处理返回值
        auto it = nodeToVReg_.find(node);
        if (it != nodeToVReg_.end())
        {
            Register dst = it->second;
            Register srcReg = (dst.dt == BE::F32 || dst.dt == BE::F64) ? PR::fa0 : PR::a0;
            m_block->insts.push_back(createMove(new RegOperand(dst), new RegOperand(srcReg), LOC_STR));
        }
    }

    void DAGIsel::selectRet(const DAG::SDNode* node, BE::Block* m_block)
    {
        // 操作数 0 是 Chain（保证副作用顺序），操作数 1 是实际返回值
        // 如有返回值，则将返回值移动到 a0 / fa0
        if (node->getNumOperands() > 1)
        {
            const DAG::SDNode* retValNode = node->getOperand(1).getNode();

            Register retReg = getOperandReg(retValNode, m_block);

            DataType* retType = retValNode->getNumValues() > 0 ? retValNode->getValueType(0) : BE::I32;
            Register  destReg = (retType == BE::F32 || retType == BE::F64) ? PR::fa0 : PR::a0;

            m_block->insts.push_back(createMove(new RegOperand(destReg), new RegOperand(retReg), LOC_STR));
        }

        m_block->insts.push_back(createIInst(Operator::JALR, PR::x0, PR::ra, 0));
    }

    void DAGIsel::selectCast(const DAG::SDNode* node, BE::Block* m_block)
    {
        if (node->getNumOperands() < 1) return;

        auto opcode = static_cast<DAG::ISD>(node->getOpcode());
        Register dst = nodeToVReg_.at(node);

        const DAG::SDNode* srcNode = node->getOperand(0).getNode();
        if (!srcNode) return;

        Register srcReg = getOperandReg(srcNode, m_block);

        switch (opcode)
        {
            case DAG::ISD::ZEXT:
                // 零扩展: 使用 ZEXT.W 或 SLLI + SRLI
                m_block->insts.push_back(createR2Inst(Operator::ZEXT_W, dst, srcReg));
                break;

            case DAG::ISD::SITOFP:
                // 有符号整数转浮点
                m_block->insts.push_back(createR2Inst(Operator::FCVT_S_W, dst, srcReg));
                break;

            case DAG::ISD::FPTOSI:
                // 浮点转有符号整数
                m_block->insts.push_back(createR2Inst(Operator::FCVT_W_S, dst, srcReg));
                break;

            default:
                ERROR("Unsupported cast opcode: %s", DAG::toString(opcode));
        }
    }

    void DAGIsel::selectNode(const DAG::SDNode* node, BE::Block* m_block)
    {
        if (!node) return;

        auto opcode = static_cast<DAG::ISD>(node->getOpcode());

        switch (opcode)
        {
            case DAG::ISD::FRAME_INDEX:
            case DAG::ISD::CONST_I32:
            case DAG::ISD::CONST_I64:
            case DAG::ISD::CONST_F32:
            case DAG::ISD::REG:
            case DAG::ISD::LABEL:
            case DAG::ISD::SYMBOL:
            case DAG::ISD::ENTRY_TOKEN:
            case DAG::ISD::TOKEN_FACTOR: break;
            case DAG::ISD::COPY: selectCopy(node, m_block); break;
            case DAG::ISD::PHI: selectPhi(node, m_block); break;
            case DAG::ISD::ADD:
            case DAG::ISD::SUB:
            case DAG::ISD::MUL:
            case DAG::ISD::DIV:
            case DAG::ISD::MOD:
            case DAG::ISD::AND:
            case DAG::ISD::OR:
            case DAG::ISD::XOR:
            case DAG::ISD::SHL:
            case DAG::ISD::ASHR:
            case DAG::ISD::LSHR:
            case DAG::ISD::FADD:
            case DAG::ISD::FSUB:
            case DAG::ISD::FMUL:
            case DAG::ISD::FDIV: selectBinary(node, m_block); break;
            case DAG::ISD::LOAD: selectLoad(node, m_block); break;
            case DAG::ISD::STORE: selectStore(node, m_block); break;
            case DAG::ISD::ICMP: selectICmp(node, m_block); break;
            case DAG::ISD::FCMP: selectFCmp(node, m_block); break;
            case DAG::ISD::BR:
            case DAG::ISD::BRCOND: selectBranch(node, m_block); break;
            case DAG::ISD::CALL: selectCall(node, m_block); break;
            case DAG::ISD::RET: selectRet(node, m_block); break;
            case DAG::ISD::ZEXT:
            case DAG::ISD::SITOFP:
            case DAG::ISD::FPTOSI: selectCast(node, m_block); break;

            default: ERROR("Unsupported DAG node: %s", DAG::toString(static_cast<DAG::ISD>(node->getOpcode())));
        }
    }

    void DAGIsel::selectBlock(ME::Block* ir_block, const DAG::SelectionDAG& dag)
    {
        // 获取当前 MIR 基本块
        BE::Block* m_block = ctx_.mfunc->blocks[static_cast<uint32_t>(ir_block->blockId)];

        // 重置块级状态
        nodeToVReg_.clear();
        selected_.clear();

        // 阶段 1：调度 DAG 节点
        auto scheduledNodes = scheduleDAG(dag);

        // 阶段 1.5：为每个节点预分配虚拟寄存器
        for (const auto* node : scheduledNodes)
            allocateRegistersForNode(node);

        // 阶段 2：指令选择
        for (const auto* node : scheduledNodes)
        {
            if (selected_.count(node)) continue;
            selected_.insert(node);
            selectNode(node, m_block);
        }
    }

    void DAGIsel::selectFunction(ME::Function* ir_func)
    {
        // 1. 重置函数级上下文
        ctx_.mfunc = nullptr;
        ctx_.vregMap.clear();
        ctx_.allocaFI.clear();

        // 2. 创建后端函数对象
        std::string funcName = ir_func->funcDef->funcName;
        ctx_.mfunc = new BE::Function(funcName);
        m_backend_module->functions.push_back(ctx_.mfunc);

        // 3. 计算传出参数区大小（遍历所有 CALL 指令找最大参数数量）
        //    为前 8 个寄存器参数预留临时区，避免搬运时被覆盖
        int maxCallBytes = 0;
        for (auto& [blockId, block] : ir_func->blocks)
        {
            for (auto* inst : block->insts)
            {
                if (auto* call = dynamic_cast<ME::CallInst*>(inst))
                {
                    int argCount      = static_cast<int>(call->args.size());
                    int stackArgBytes = std::max(0, argCount - 8) * 8;
                    int regTempBytes  = std::min(argCount, 8) * 8;  // 为 a0-a7 预留的临时区
                    int totalBytes    = stackArgBytes + regTempBytes;
                    if (totalBytes > maxCallBytes) maxCallBytes = totalBytes;
                }
            }
        }
        ctx_.mfunc->paramSize = maxCallBytes;
        ctx_.mfunc->frameInfo.setParamAreaSize(ctx_.mfunc->paramSize);

        // 4. 收集局部变量（alloca）
        collectAllocas(ir_func);

        // 5. 创建所有基本块的 MIR 对象
        for (auto& [blockId, block] : ir_func->blocks)
        {
            auto* m_block = new BE::Block(static_cast<uint32_t>(blockId));
            ctx_.mfunc->blocks[static_cast<uint32_t>(blockId)] = m_block;
        }

        // 6. 为参数分配虚拟寄存器
        setupParameters(ir_func);

        // 7. 对每个基本块构建 DAG 并做指令选择
        for (auto& [blockId, block] : ir_func->blocks)
        {
            // 获取该块的 SelectionDAG（由 target_->buildDAG 预先构建）
            auto it = target_->block_dags.find(block);
            if (it != target_->block_dags.end() && it->second)
                selectBlock(block, *(it->second));
        }
    }

    void DAGIsel::runImpl()
    {
        importGlobals();

        target_->buildDAG(ir_module_);

        for (auto* f : ir_module_->functions) selectFunction(f);
    }

}  // namespace BE::RV64
