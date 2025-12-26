#include <middleend/visitor/utils/sccp_visitor.h>
#include <middleend/pass/sccp.h>
#include <middleend/module/ir_operand.h>
#include <cmath>
#include <cstdint>

namespace ME
{
    // 更新目标寄存器格值，并将受影响的指令压入工作队列
    // 这里会做一次合并判断，只有格值变化才会触发后续传播
    // 避免重复入队导致不必要的遍历
    void SCCPEvalVisitor::updateValue(SCCPPass& pass, Operand* dest, const SCCPPass::LatticeVal& val)
    {
        if (!dest || dest->getType() != OperandType::REG) return;   // 仅处理寄存器目标
        size_t reg    = dest->getRegNum(); // 目标寄存器编号
        // 获取当前格值并合并新值
        auto   curr   = getValue(pass, dest); // 当前格值
        auto   merged = mergeValue(curr, val); // 合并后格值

        // 合并后与原值比较，决定是否需要继续向后传播
        // 由于 mergeValue 保证单调性，只需检查 kind 是否提升
        
        // 类型不同则一定变化，类型相同则具体值变化才算变化
        bool changed = (curr.kind != merged.kind);
        // 如果 kind 相同且都是 CONST，还需比较具体常量值
        if (!changed && merged.kind == SCCPPass::LatticeKind::CONST)
        {
            if (curr.type == DataType::F32 && merged.type == DataType::F32){
                changed = (curr.f32 != merged.f32);
            }
            else{
                changed = (curr.i32 != merged.i32);
            }
        }
        if (changed)
        {
             // 只有值变更才通知使用者，减少无效迭代
            pass.valueMap[reg] = merged;
           
            auto it = pass.userMap.find(reg);
            if (it != pass.userMap.end())
            {
                // 将所有使用该寄存器的指令加入工作队列
                for (auto* user : it->second) {
                    pass.instWorklist.push_back(user);
                }
            }
        }
    }

    // 标记边可达，同时推进后继块或 Phi 指令
    // 这样可以增量地触发后继块的分析
    // 保证工作队列按需扩展
    void SCCPEvalVisitor::markEdgeReachable(SCCPPass& pass, size_t from, size_t to)
    {
        if (!pass.currFunc) return;
        // 插入可达边集合，若已存在则不处理
        if (!pass.reachableEdges.insert({from, to}).second) return;

        // 获得后继基本块指针
        Block* succ = pass.currFunc->getBlock(to);
        if (!succ) return;

        // 将后继块加入可达块集合与工作队列
        if (pass.reachableBlocks.insert(to).second) { pass.blockWorklist.push_back(succ); }
        else
        {
            // 如果后继块已在可达集合中，
            // 仅入队 Phi 指令，保持 SSA 合并点更新
            for (auto* inst : succ->insts)
            {
                if (inst->opcode != Operator::PHI) break;
                pass.instWorklist.push_back(inst);
            }
        }
    }

    // 下面是一组 lattice 构造与求值工具
    SCCPPass::LatticeVal SCCPEvalVisitor::makeUndef() const
    {
        SCCPPass::LatticeVal val;
        val.kind = SCCPPass::LatticeKind::UNDEF;
        val.type = DataType::UNK;
        return val;
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::makeOverdefined() const
    {
        SCCPPass::LatticeVal val;
        val.kind = SCCPPass::LatticeKind::OVERDEFINED;
        val.type = DataType::UNK;
        return val;
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::makeConstInt(int value) const
    {
        SCCPPass::LatticeVal val;
        val.kind = SCCPPass::LatticeKind::CONST;
        val.type = DataType::I32;
        val.i32  = value;
        return val;
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::makeConstFloat(float value) const
    {
        SCCPPass::LatticeVal val;
        val.kind = SCCPPass::LatticeKind::CONST;
        val.type = DataType::F32;
        val.f32  = value;
        return val;
    }

    // 获取操作数对应的格值
    SCCPPass::LatticeVal SCCPEvalVisitor::getValue(SCCPPass& pass, Operand* op) const
    {
        if (!op) return makeUndef();
        // 立即数直接提升为常量格值
        if (op->getType() == OperandType::IMMEI32)
        {
            // 整型立即数
            auto* imm = static_cast<ImmeI32Operand*>(op);
            return makeConstInt(imm->value); 
        }
        if (op->getType() == OperandType::IMMEF32)
        {
            // 浮点型立即数
            auto* imm = static_cast<ImmeF32Operand*>(op);
            return makeConstFloat(imm->value);
        }
        if (op->getType() == OperandType::REG)
        {
            // 寄存器从 lattice 表中读取，不存在则视为 UNDEF
            size_t reg = op->getRegNum();
            auto   it  = pass.valueMap.find(reg);
            if (it != pass.valueMap.end()) return it->second;
            return makeUndef();
        }
        // 其他操作数类型视为 OVERDEFINED
        return makeOverdefined();
    }

    // mergeValue 主要用于合并两个格值
    // 保证单调性：UNDEF < CONST < OVERDEFINED
    SCCPPass::LatticeVal SCCPEvalVisitor::mergeValue(
        const SCCPPass::LatticeVal& lhs, const SCCPPass::LatticeVal& rhs) const
    {
        // OVERDEFINED 覆盖所有情况
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
            return makeOverdefined();
        // UNDEF 与常量合并结果为常量，这里 表示尚未有值流入， 直接采用另一个值
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF) return rhs;
        if (rhs.kind == SCCPPass::LatticeKind::UNDEF) return lhs;
        if (lhs.kind == SCCPPass::LatticeKind::CONST && rhs.kind == SCCPPass::LatticeKind::CONST)
        {
            // 如果两个都是常量，则需要类型相等才能合并，否则为 OVERDEFINED
            if (lhs.type != rhs.type) return makeOverdefined();
            if (lhs.type == DataType::F32)
            {
                // 如果浮点值相等则合并，否则为 OVERDEFINED
                if (lhs.f32 == rhs.f32) return lhs;
                return makeOverdefined();
            }
            // 整数类型，值相等则合并，否则为 OVERDEFINED
            if (lhs.i32 == rhs.i32) return lhs;
            return makeOverdefined();
        }
        return makeOverdefined();
    }

    void SCCPEvalVisitor::visit(LoadInst& inst, SCCPPass& pass, Block* block)
    {
        // Load/Alloca/GEP 默认过定义，SCCP 不做内存求值
        // 这三种指令的结果都涉及内存地址或内存读取，SCCP 作为一个纯粹的数据流分析无法精确追踪
        updateValue(pass, inst.res, makeOverdefined());
    }
    void SCCPEvalVisitor::visit(StoreInst& inst, SCCPPass& pass, Block* block) { (void)inst; }
    void SCCPEvalVisitor::visit(ArithmeticInst& inst, SCCPPass& pass, Block* block)
    {
        // 常量算术折叠
        // 获取操作数格值
        auto lhs = getValue(pass, inst.lhs);
        auto rhs = getValue(pass, inst.rhs);

        // 先处理 lattice 状态，避免错误传播
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
        {
            // 特殊情况：即使有 OVERDEFINED，某些运算仍可确定结果
            // x * 0 = 0, 0 * x = 0
            if (inst.opcode == Operator::MUL)
            {
                if ((lhs.kind == SCCPPass::LatticeKind::CONST && lhs.type != DataType::F32 && lhs.i32 == 0) ||
                    (rhs.kind == SCCPPass::LatticeKind::CONST && rhs.type != DataType::F32 && rhs.i32 == 0))
                {
                    updateValue(pass, inst.res, makeConstInt(0));
                    return;
                }
            }
            // x & 0 = 0, 0 & x = 0
            if (inst.opcode == Operator::BITAND)
            {
                if ((lhs.kind == SCCPPass::LatticeKind::CONST && lhs.type != DataType::F32 && lhs.i32 == 0) ||
                    (rhs.kind == SCCPPass::LatticeKind::CONST && rhs.type != DataType::F32 && rhs.i32 == 0))
                {
                    updateValue(pass, inst.res, makeConstInt(0));
                    return;
                }
            }
            // 下面的运算无法确定结果
            // x ^ 0 = x, 但需要 x 是常量才能确定结果
            // x & -1 = x, 但需要 x 是常量才能确定结果
            updateValue(pass, inst.res, makeOverdefined());
            return;
        }
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF || rhs.kind == SCCPPass::LatticeKind::UNDEF)
        {
            // 特殊情况：即使有 UNDEF，某些运算仍可确定结果
            // x * 0 = 0, 0 * x = 0
            if (inst.opcode == Operator::MUL)
            {
                if ((lhs.kind == SCCPPass::LatticeKind::CONST && lhs.type != DataType::F32 && lhs.i32 == 0) ||
                    (rhs.kind == SCCPPass::LatticeKind::CONST && rhs.type != DataType::F32 && rhs.i32 == 0))
                {
                    updateValue(pass, inst.res, makeConstInt(0));
                    return;
                }
            }
            // x & 0 = 0, 0 & x = 0
            if (inst.opcode == Operator::BITAND)
            {
                if ((lhs.kind == SCCPPass::LatticeKind::CONST && lhs.type != DataType::F32 && lhs.i32 == 0) ||
                    (rhs.kind == SCCPPass::LatticeKind::CONST && rhs.type != DataType::F32 && rhs.i32 == 0))
                {
                    updateValue(pass, inst.res, makeConstInt(0));
                    return;
                }
            }
            updateValue(pass, inst.res, makeUndef());
            return;
        }

        // 浮点运算
        if (inst.opcode == Operator::FADD || inst.opcode == Operator::FSUB || inst.opcode == Operator::FMUL ||
            inst.opcode == Operator::FDIV || inst.dt == DataType::F32)
        {
            // 将操作数转为浮点数进行计算
            float l = lhs.type == DataType::F32 ? lhs.f32 : static_cast<float>(lhs.i32);
            float r = rhs.type == DataType::F32 ? rhs.f32 : static_cast<float>(rhs.i32);
            if (inst.opcode == Operator::FADD) { updateValue(pass, inst.res, makeConstFloat(l + r)); return; }
            if (inst.opcode == Operator::FSUB) { updateValue(pass, inst.res, makeConstFloat(l - r)); return; }
            if (inst.opcode == Operator::FMUL) { updateValue(pass, inst.res, makeConstFloat(l * r)); return; }
            // 这里不对除零做特殊处理，因为在IEEE 754 浮点标准中，浮点数除以零是有定义的行为，不会产生异常
            if (inst.opcode == Operator::FDIV) { updateValue(pass, inst.res, makeConstFloat(l / r)); return; }
            updateValue(pass, inst.res, makeOverdefined());
            return;
        }

        // 整数运算 需处理除零与位操作
        int32_t l = static_cast<int32_t>(lhs.i32);
        int32_t r = static_cast<int32_t>(rhs.i32);
        if (inst.opcode == Operator::ADD) { updateValue(pass, inst.res, makeConstInt(static_cast<int>(l + r))); return; }
        if (inst.opcode == Operator::SUB) { updateValue(pass, inst.res, makeConstInt(static_cast<int>(l - r))); return; }
        if (inst.opcode == Operator::MUL) { updateValue(pass, inst.res, makeConstInt(static_cast<int>(l * r))); return; }
        if (inst.opcode == Operator::DIV)
        {
            if (r == 0) { updateValue(pass, inst.res, makeOverdefined()); return; }
            updateValue(pass, inst.res, makeConstInt(static_cast<int>(l / r)));
            return;
        }
        if (inst.opcode == Operator::MOD)
        {
            if (r == 0) { updateValue(pass, inst.res, makeOverdefined()); return; }
            updateValue(pass, inst.res, makeConstInt(static_cast<int>(l % r)));
            return;
        }
        if (inst.opcode == Operator::BITXOR) { updateValue(pass, inst.res, makeConstInt(static_cast<int>(l ^ r))); return; }
        if (inst.opcode == Operator::BITAND) { updateValue(pass, inst.res, makeConstInt(static_cast<int>(l & r))); return; }
        // 位移操作仅考虑低5位移位数，也就是说最大移位31位
        if (inst.opcode == Operator::SHL) { updateValue(pass, inst.res, makeConstInt(static_cast<int>(l << (r & 31)))); return; }
        // ASHR算数右移：保留符号位，LSHR逻辑右移：不保留符号位
        if (inst.opcode == Operator::ASHR) { updateValue(pass, inst.res, makeConstInt(static_cast<int>(l >> (r & 31)))); return; }
        if (inst.opcode == Operator::LSHR)
        {
            uint32_t ul = static_cast<uint32_t>(l);
            updateValue(pass, inst.res, makeConstInt(static_cast<int>(ul >> (r & 31))));
            return;
        }
        updateValue(pass, inst.res, makeOverdefined());
    }
    void SCCPEvalVisitor::visit(IcmpInst& inst, SCCPPass& pass, Block* block)
    {
        // 整数常量比较折叠
        auto lhs = getValue(pass, inst.lhs);
        auto rhs = getValue(pass, inst.rhs);

        // 比较类指令同样先判断 lattice 状态
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
        {
            updateValue(pass, inst.res, makeOverdefined());
            return;
        }
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF || rhs.kind == SCCPPass::LatticeKind::UNDEF)
        {
            updateValue(pass, inst.res, makeUndef());
            return;
        }

        int32_t l   = static_cast<int32_t>(lhs.i32);
        int32_t r   = static_cast<int32_t>(rhs.i32);
        bool    res = false;

        // 逐个分支判断条件码
        if (inst.cond == ICmpOp::EQ)
            res = (l == r);
        else if (inst.cond == ICmpOp::NE)
            res = (l != r);
        else if (inst.cond == ICmpOp::SGT)
            res = (l > r);
        else if (inst.cond == ICmpOp::SGE)
            res = (l >= r);
        else if (inst.cond == ICmpOp::SLT)
            res = (l < r);
        else if (inst.cond == ICmpOp::SLE)
            res = (l <= r);
        else if (inst.cond == ICmpOp::UGT)
            res = (static_cast<uint32_t>(l) > static_cast<uint32_t>(r));
        else if (inst.cond == ICmpOp::UGE)
            res = (static_cast<uint32_t>(l) >= static_cast<uint32_t>(r));
        else if (inst.cond == ICmpOp::ULT)
            res = (static_cast<uint32_t>(l) < static_cast<uint32_t>(r));
        else if (inst.cond == ICmpOp::ULE)
            res = (static_cast<uint32_t>(l) <= static_cast<uint32_t>(r));
        else
        {
            updateValue(pass, inst.res, makeOverdefined());
            return;
        }

        updateValue(pass, inst.res, makeConstInt((int)res));
    }
    void SCCPEvalVisitor::visit(FcmpInst& inst, SCCPPass& pass, Block* block)
    {
        // 浮点 常量比较折叠
        auto lhs = getValue(pass, inst.lhs);
        auto rhs = getValue(pass, inst.rhs);

        // 浮点比较需处理 NaN 情况
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
        {
            updateValue(pass, inst.res, makeOverdefined());
            return;
        }
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF || rhs.kind == SCCPPass::LatticeKind::UNDEF)
        {
            updateValue(pass, inst.res, makeUndef());
            return;
        }

        float l    = lhs.type == DataType::F32 ? lhs.f32 : static_cast<float>(lhs.i32);
        float r    = rhs.type == DataType::F32 ? rhs.f32 : static_cast<float>(rhs.i32);
        bool  lnan = std::isnan(l);
        bool  rnan = std::isnan(r);
        bool  res  = false;

        // 按 IEEE 规则处理有序/无序比较
        if (inst.cond == FCmpOp::OEQ)
            res = (!lnan && !rnan && l == r);
        else if (inst.cond == FCmpOp::OGT)
            res = (!lnan && !rnan && l > r);
        else if (inst.cond == FCmpOp::OGE)
            res = (!lnan && !rnan && l >= r);
        else if (inst.cond == FCmpOp::OLT)
            res = (!lnan && !rnan && l < r);
        else if (inst.cond == FCmpOp::OLE)
            res = (!lnan && !rnan && l <= r);
        else if (inst.cond == FCmpOp::ONE)
            res = (!lnan && !rnan && l != r);
        else if (inst.cond == FCmpOp::ORD)
            res = (!lnan && !rnan);
        else if (inst.cond == FCmpOp::UEQ)
            res = (lnan || rnan || l == r);
        else if (inst.cond == FCmpOp::UGT)
            res = (lnan || rnan || l > r);
        else if (inst.cond == FCmpOp::UGE)
            res = (lnan || rnan || l >= r);
        else if (inst.cond == FCmpOp::ULT)
            res = (lnan || rnan || l < r);
        else if (inst.cond == FCmpOp::ULE)
            res = (lnan || rnan || l <= r);
        else if (inst.cond == FCmpOp::UNE)
            res = (lnan || rnan || l != r);
        else if (inst.cond == FCmpOp::UNO)
            res = (lnan || rnan);
        else
        {
            updateValue(pass, inst.res, makeOverdefined());
            return;
        }

        updateValue(pass, inst.res, makeConstInt((int)res));
    }
    void SCCPEvalVisitor::visit(AllocaInst& inst, SCCPPass& pass, Block* block)
    {
        updateValue(pass, inst.res, makeOverdefined());
    }
    void SCCPEvalVisitor::visit(BrCondInst& inst, SCCPPass& pass, Block* block)
    {
        // 条件分支可达边标记
        if (!block) return;
        if (!inst.trueTar || !inst.falseTar) return;
        if (inst.trueTar->getType() != OperandType::LABEL || inst.falseTar->getType() != OperandType::LABEL) return;

        // 获得目标块编号
        size_t trueId  = inst.trueTar->getLabelNum();
        size_t falseId = inst.falseTar->getLabelNum();

        // 分支条件若为常量可直接确定可达边
        auto condVal = getValue(pass, inst.cond);
        if (condVal.kind == SCCPPass::LatticeKind::CONST && condVal.type != DataType::F32)
        {
            if (condVal.i32 != 0){
                markEdgeReachable(pass, block->blockId, trueId);
            }
            else{
                markEdgeReachable(pass, block->blockId, falseId);
            }
            return;
        }

        // 不确定就将所有边都标记为可达
        markEdgeReachable(pass, block->blockId, trueId);
        markEdgeReachable(pass, block->blockId, falseId);
    }
    void SCCPEvalVisitor::visit(BrUncondInst& inst, SCCPPass& pass, Block* block)
    {
        if (!block) return;
        if (!inst.target || inst.target->getType() != OperandType::LABEL) return;
        // 将无条件分支目标边标记为可达
        markEdgeReachable(pass, block->blockId, inst.target->getLabelNum());
    }
    void SCCPEvalVisitor::visit(GlbVarDeclInst& inst, SCCPPass& pass, Block* block) { (void)inst; }
    void SCCPEvalVisitor::visit(CallInst& inst, SCCPPass& pass, Block* block)
    {
        // 调用结果视为不确定
        updateValue(pass, inst.res, makeOverdefined());
    }
    void SCCPEvalVisitor::visit(FuncDeclInst& inst, SCCPPass& pass, Block* block) { (void)inst; }
    void SCCPEvalVisitor::visit(FuncDefInst& inst, SCCPPass& pass, Block* block) { (void)inst; }
    void SCCPEvalVisitor::visit(RetInst& inst, SCCPPass& pass, Block* block) { (void)inst; }
    void SCCPEvalVisitor::visit(GEPInst& inst, SCCPPass& pass, Block* block)
    {
        // GEP 计算地址，不做常量求值
        updateValue(pass, inst.res, makeOverdefined());
    }
    void SCCPEvalVisitor::visit(FP2SIInst& inst, SCCPPass& pass, Block* block)
    {
        // 浮点转整型常量折叠
        auto src = getValue(pass, inst.src);
        if (src.kind == SCCPPass::LatticeKind::OVERDEFINED) { updateValue(pass, inst.dest, makeOverdefined()); return; }
        if (src.kind == SCCPPass::LatticeKind::UNDEF) { updateValue(pass, inst.dest, makeUndef()); return; }

        float val = src.type == DataType::F32 ? src.f32 : static_cast<float>(src.i32);
        updateValue(pass, inst.dest, makeConstInt(static_cast<int>(val)));
    }
    void SCCPEvalVisitor::visit(SI2FPInst& inst, SCCPPass& pass, Block* block)
    {
        // 整形转浮点
        auto src = getValue(pass, inst.src);
        if (src.kind == SCCPPass::LatticeKind::OVERDEFINED) { updateValue(pass, inst.dest, makeOverdefined()); return; }
        if (src.kind == SCCPPass::LatticeKind::UNDEF) { updateValue(pass, inst.dest, makeUndef()); return; }

        int val = src.type == DataType::F32 ? static_cast<int>(src.f32) : src.i32;
        updateValue(pass, inst.dest, makeConstFloat(static_cast<float>(val)));
    }
    void SCCPEvalVisitor::visit(ZextInst& inst, SCCPPass& pass, Block* block)
    {
        // 零扩展常量折叠
        auto src = getValue(pass, inst.src);
        if (src.kind == SCCPPass::LatticeKind::OVERDEFINED) { updateValue(pass, inst.dest, makeOverdefined()); return; }
        if (src.kind == SCCPPass::LatticeKind::UNDEF) { updateValue(pass, inst.dest, makeUndef()); return; }

        updateValue(pass, inst.dest, makeConstInt(src.type == DataType::F32 ? static_cast<int>(src.f32) : src.i32));
    }
    void SCCPEvalVisitor::visit(PhiInst& inst, SCCPPass& pass, Block* block)
    {
        // Phi 只合并可达前驱的格值
        if (!block) { 
            // 如果块不存在，结果视为 UNDEF
            updateValue(pass, inst.res, makeUndef()); 
            return; 
        }
        SCCPPass::LatticeVal result      = makeUndef();
        bool                 hasIncoming = false;

        // 只合并可达前驱，避免不可达路径污染
        for (auto& [labelOp, valOp] : inst.incomingVals)
        {
            if (!labelOp || labelOp->getType() != OperandType::LABEL) continue;
            size_t predId = labelOp->getLabelNum();
            // 如果前驱块不可达则跳过
            if (pass.reachableEdges.count({predId, block->blockId}) == 0) continue;

            //可达前驱，合并其格值
            hasIncoming = true;
            result      = mergeValue(result, getValue(pass, valOp));
            // overdefined 后续无需继续合并
            if (result.kind == SCCPPass::LatticeKind::OVERDEFINED) break;
        }

        if (!hasIncoming) {
            // 没有可达前驱，结果视为 UNDEF
            updateValue(pass, inst.res, makeUndef()); 
            return; 
        }
        // 更新 Phi 结果格值
        updateValue(pass, inst.res, result);
    }

    // 把已知 寄存器操作数替换为对应常量
    void SCCPReplaceVisitor::replaceOperandIfConst(SCCPPass& pass, Operand*& op)
    {
        if (!op || op->getType() != OperandType::REG) return;

        // 获取对应格值
        auto it = pass.valueMap.find(op->getRegNum());
        if (it == pass.valueMap.end()) return;
        const auto& val = it->second;

        // 只有常量格值才替换
        if (val.kind != SCCPPass::LatticeKind::CONST) return;

        // 直接替换为对应的立即数操作数
        if (val.type == DataType::F32){
            op = getImmeF32Operand(val.f32);
        }
        else{
            op = getImmeI32Operand(val.i32);
        }
    }

    void SCCPReplaceVisitor::visit(LoadInst& inst, SCCPPass& pass) { 
        replaceOperandIfConst(pass, inst.ptr); 
    }
    void SCCPReplaceVisitor::visit(StoreInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.ptr);
        replaceOperandIfConst(pass, inst.val);
    }
    void SCCPReplaceVisitor::visit(ArithmeticInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.lhs);
        replaceOperandIfConst(pass, inst.rhs);
    }
    void SCCPReplaceVisitor::visit(IcmpInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.lhs);
        replaceOperandIfConst(pass, inst.rhs);
    }
    void SCCPReplaceVisitor::visit(FcmpInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.lhs);
        replaceOperandIfConst(pass, inst.rhs);
    }
    void SCCPReplaceVisitor::visit(AllocaInst& inst, SCCPPass& pass) { (void)inst; }
    void SCCPReplaceVisitor::visit(BrCondInst& inst, SCCPPass& pass) { replaceOperandIfConst(pass, inst.cond); }
    void SCCPReplaceVisitor::visit(BrUncondInst& inst, SCCPPass& pass) { (void)inst; }
    void SCCPReplaceVisitor::visit(GlbVarDeclInst& inst, SCCPPass& pass) { replaceOperandIfConst(pass, inst.init); }
    void SCCPReplaceVisitor::visit(CallInst& inst, SCCPPass& pass)
    {
        for (auto& arg : inst.args) {
            replaceOperandIfConst(pass, arg.second);
        }
    }
    void SCCPReplaceVisitor::visit(FuncDeclInst& inst, SCCPPass& pass) { (void)inst; }
    void SCCPReplaceVisitor::visit(FuncDefInst& inst, SCCPPass& pass) { (void)inst; }
    void SCCPReplaceVisitor::visit(RetInst& inst, SCCPPass& pass) { replaceOperandIfConst(pass, inst.res); }
    void SCCPReplaceVisitor::visit(GEPInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.basePtr);
        for (auto& idx : inst.idxs) replaceOperandIfConst(pass, idx);
    }
    void SCCPReplaceVisitor::visit(FP2SIInst& inst, SCCPPass& pass) { replaceOperandIfConst(pass, inst.src); }
    void SCCPReplaceVisitor::visit(SI2FPInst& inst, SCCPPass& pass) { replaceOperandIfConst(pass, inst.src); }
    void SCCPReplaceVisitor::visit(ZextInst& inst, SCCPPass& pass) { replaceOperandIfConst(pass, inst.src); }
    void SCCPReplaceVisitor::visit(PhiInst& inst, SCCPPass& pass)
    {
        for (auto& [label, val] : inst.incomingVals) { replaceOperandIfConst(pass, val); }
    }

}  // namespace ME
