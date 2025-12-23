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
        if (!dest || dest->getType() != OperandType::REG) return;
        size_t reg     = static_cast<RegOperand*>(dest)->regNum;
        auto   curr    = getValue(pass, dest);
        auto merged = mergeValue(curr, val);
        // 合并后与原值比较，决定是否需要继续向后传播
        bool changed = true;
        if (curr.kind == merged.kind)
        {
            if (curr.kind != SCCPPass::LatticeKind::CONST) changed = false;
            else if (curr.type == merged.type)
            {
                if (curr.type == DataType::F32) changed = (curr.f32 != merged.f32);
                else changed = (curr.i32 != merged.i32);
            }
        }
        if (changed)
        {
            pass.valueMap[reg] = merged;
            // 只有值变更才通知使用者，减少无效迭代
            auto it = pass.userMap.find(reg);
            if (it != pass.userMap.end())
            {
                for (auto* user : it->second) pass.instWorklist.push_back(user);
            }
        }
    }

    // 标记边可达，同时推进后继块或 Phi 指令
    // 这样可以增量地触发后继块的分析
    // 保证工作队列按需扩展
    void SCCPEvalVisitor::markEdgeReachable(SCCPPass& pass, size_t from, size_t to)
    {
        if (!pass.currFunc) return;
        if (!pass.reachableEdges.insert({from, to}).second) return;

        Block* succ = pass.currFunc->getBlock(to);
        if (!succ) return;

        if (pass.reachableBlocks.insert(to).second)
        {
            pass.blockWorklist.push_back(succ);
        }
        else
        {
            // 仅入队 Phi，保持 SSA 合并点更新
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

    SCCPPass::LatticeVal SCCPEvalVisitor::getValue(SCCPPass& pass, Operand* op) const
    {
        if (!op) return makeUndef();
        // 立即数直接提升为常量格值
        if (op->getType() == OperandType::IMMEI32)
        {
            auto* imm = static_cast<ImmeI32Operand*>(op);
            return makeConstInt(imm->value);
        }
        if (op->getType() == OperandType::IMMEF32)
        {
            auto* imm = static_cast<ImmeF32Operand*>(op);
            return makeConstFloat(imm->value);
        }
        if (op->getType() == OperandType::REG)
        {
            // 寄存器从 lattice 表中读取，不存在则视为 UNDEF
            size_t reg = static_cast<RegOperand*>(op)->regNum;
            auto   it  = pass.valueMap.find(reg);
            if (it != pass.valueMap.end()) return it->second;
            return makeUndef();
        }
        return makeOverdefined();
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::mergeValue(const SCCPPass::LatticeVal& lhs,
                                                     const SCCPPass::LatticeVal& rhs) const
    {
        // OVERDEFINED 覆盖所有情况
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
            return makeOverdefined();
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF) return rhs;
        if (rhs.kind == SCCPPass::LatticeKind::UNDEF) return lhs;
        if (lhs.kind == SCCPPass::LatticeKind::CONST && rhs.kind == SCCPPass::LatticeKind::CONST)
        {
            if (lhs.type != rhs.type) return makeOverdefined();
            if (lhs.type == DataType::F32)
            {
                if (lhs.f32 == rhs.f32) return lhs;
                return makeOverdefined();
            }
            if (lhs.i32 == rhs.i32) return lhs;
            return makeOverdefined();
        }
        return makeOverdefined();
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::evalArithmetic(SCCPPass& pass, ArithmeticInst& inst) const
    {
        auto lhs = getValue(pass, inst.lhs);
        auto rhs = getValue(pass, inst.rhs);

        // 先处理 lattice 状态，避免错误传播
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
            return makeOverdefined();
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF || rhs.kind == SCCPPass::LatticeKind::UNDEF) return makeUndef();
        if (lhs.kind != SCCPPass::LatticeKind::CONST || rhs.kind != SCCPPass::LatticeKind::CONST)
            return makeOverdefined();

        // 浮点运算统一走浮点路径
        if (inst.opcode == Operator::FADD || inst.opcode == Operator::FSUB || inst.opcode == Operator::FMUL ||
            inst.opcode == Operator::FDIV || inst.dt == DataType::F32)
        {
            float l = lhs.type == DataType::F32 ? lhs.f32 : static_cast<float>(lhs.i32);
            float r = rhs.type == DataType::F32 ? rhs.f32 : static_cast<float>(rhs.i32);
            if (inst.opcode == Operator::FADD) return makeConstFloat(l + r);
            if (inst.opcode == Operator::FSUB) return makeConstFloat(l - r);
            if (inst.opcode == Operator::FMUL) return makeConstFloat(l * r);
            if (inst.opcode == Operator::FDIV) return makeConstFloat(l / r);
            return makeOverdefined();
        }

        // 整数路径需处理除零与位操作
        int32_t l = static_cast<int32_t>(lhs.i32);
        int32_t r = static_cast<int32_t>(rhs.i32);
        if (inst.opcode == Operator::ADD) return makeConstInt(static_cast<int>(l + r));
        if (inst.opcode == Operator::SUB) return makeConstInt(static_cast<int>(l - r));
        if (inst.opcode == Operator::MUL) return makeConstInt(static_cast<int>(l * r));
        if (inst.opcode == Operator::DIV)
        {
            if (r == 0) return makeOverdefined();
            return makeConstInt(static_cast<int>(l / r));
        }
        if (inst.opcode == Operator::MOD)
        {
            if (r == 0) return makeOverdefined();
            return makeConstInt(static_cast<int>(l % r));
        }
        if (inst.opcode == Operator::BITXOR) return makeConstInt(static_cast<int>(l ^ r));
        if (inst.opcode == Operator::BITAND) return makeConstInt(static_cast<int>(l & r));
        if (inst.opcode == Operator::SHL) return makeConstInt(static_cast<int>(l << (r & 31)));
        if (inst.opcode == Operator::ASHR) return makeConstInt(static_cast<int>(l >> (r & 31)));
        if (inst.opcode == Operator::LSHR)
        {
            uint32_t ul = static_cast<uint32_t>(l);
            return makeConstInt(static_cast<int>(ul >> (r & 31)));
        }
        return makeOverdefined();
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::evalIcmp(SCCPPass& pass, IcmpInst& inst) const
    {
        auto lhs = getValue(pass, inst.lhs);
        auto rhs = getValue(pass, inst.rhs);

        // 比较类指令同样先判断 lattice 状态
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
            return makeOverdefined();
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF || rhs.kind == SCCPPass::LatticeKind::UNDEF) return makeUndef();
        if (lhs.kind != SCCPPass::LatticeKind::CONST || rhs.kind != SCCPPass::LatticeKind::CONST)
            return makeOverdefined();

        int32_t l = static_cast<int32_t>(lhs.i32);
        int32_t r = static_cast<int32_t>(rhs.i32);
        bool    res = false;

        // 逐个分支判断条件码
        if (inst.cond == ICmpOp::EQ) res = (l == r);
        else if (inst.cond == ICmpOp::NE) res = (l != r);
        else if (inst.cond == ICmpOp::SGT) res = (l > r);
        else if (inst.cond == ICmpOp::SGE) res = (l >= r);
        else if (inst.cond == ICmpOp::SLT) res = (l < r);
        else if (inst.cond == ICmpOp::SLE) res = (l <= r);
        else if (inst.cond == ICmpOp::UGT) res = (static_cast<uint32_t>(l) > static_cast<uint32_t>(r));
        else if (inst.cond == ICmpOp::UGE) res = (static_cast<uint32_t>(l) >= static_cast<uint32_t>(r));
        else if (inst.cond == ICmpOp::ULT) res = (static_cast<uint32_t>(l) < static_cast<uint32_t>(r));
        else if (inst.cond == ICmpOp::ULE) res = (static_cast<uint32_t>(l) <= static_cast<uint32_t>(r));
        else return makeOverdefined();

        return makeConstInt(res ? 1 : 0);
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::evalFcmp(SCCPPass& pass, FcmpInst& inst) const
    {
        auto lhs = getValue(pass, inst.lhs);
        auto rhs = getValue(pass, inst.rhs);

        // 浮点比较需处理 NaN 情况
        if (lhs.kind == SCCPPass::LatticeKind::OVERDEFINED || rhs.kind == SCCPPass::LatticeKind::OVERDEFINED)
            return makeOverdefined();
        if (lhs.kind == SCCPPass::LatticeKind::UNDEF || rhs.kind == SCCPPass::LatticeKind::UNDEF) return makeUndef();
        if (lhs.kind != SCCPPass::LatticeKind::CONST || rhs.kind != SCCPPass::LatticeKind::CONST)
            return makeOverdefined();

        float l = lhs.type == DataType::F32 ? lhs.f32 : static_cast<float>(lhs.i32);
        float r = rhs.type == DataType::F32 ? rhs.f32 : static_cast<float>(rhs.i32);
        bool  lnan = std::isnan(l);
        bool  rnan = std::isnan(r);
        bool  res  = false;

        // 按 IEEE 规则处理有序/无序比较
        if (inst.cond == FCmpOp::OEQ) res = (!lnan && !rnan && l == r);
        else if (inst.cond == FCmpOp::OGT) res = (!lnan && !rnan && l > r);
        else if (inst.cond == FCmpOp::OGE) res = (!lnan && !rnan && l >= r);
        else if (inst.cond == FCmpOp::OLT) res = (!lnan && !rnan && l < r);
        else if (inst.cond == FCmpOp::OLE) res = (!lnan && !rnan && l <= r);
        else if (inst.cond == FCmpOp::ONE) res = (!lnan && !rnan && l != r);
        else if (inst.cond == FCmpOp::ORD) res = (!lnan && !rnan);
        else if (inst.cond == FCmpOp::UEQ) res = (lnan || rnan || l == r);
        else if (inst.cond == FCmpOp::UGT) res = (lnan || rnan || l > r);
        else if (inst.cond == FCmpOp::UGE) res = (lnan || rnan || l >= r);
        else if (inst.cond == FCmpOp::ULT) res = (lnan || rnan || l < r);
        else if (inst.cond == FCmpOp::ULE) res = (lnan || rnan || l <= r);
        else if (inst.cond == FCmpOp::UNE) res = (lnan || rnan || l != r);
        else if (inst.cond == FCmpOp::UNO) res = (lnan || rnan);
        else return makeOverdefined();

        return makeConstInt(res ? 1 : 0);
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::evalFP2SI(SCCPPass& pass, FP2SIInst& inst) const
    {
        auto src = getValue(pass, inst.src);
        // FP2SI 只在常量时可折叠
        if (src.kind == SCCPPass::LatticeKind::OVERDEFINED) return makeOverdefined();
        if (src.kind == SCCPPass::LatticeKind::UNDEF) return makeUndef();
        if (src.kind != SCCPPass::LatticeKind::CONST) return makeOverdefined();

        float val = src.type == DataType::F32 ? src.f32 : static_cast<float>(src.i32);
        return makeConstInt(static_cast<int>(val));
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::evalSI2FP(SCCPPass& pass, SI2FPInst& inst) const
    {
        auto src = getValue(pass, inst.src);
        // SI2FP 只在常量时可折叠
        if (src.kind == SCCPPass::LatticeKind::OVERDEFINED) return makeOverdefined();
        if (src.kind == SCCPPass::LatticeKind::UNDEF) return makeUndef();
        if (src.kind != SCCPPass::LatticeKind::CONST) return makeOverdefined();

        int val = src.type == DataType::F32 ? static_cast<int>(src.f32) : src.i32;
        return makeConstFloat(static_cast<float>(val));
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::evalZext(SCCPPass& pass, ZextInst& inst) const
    {
        auto src = getValue(pass, inst.src);
        // Zext 仅对常量执行
        if (src.kind == SCCPPass::LatticeKind::OVERDEFINED) return makeOverdefined();
        if (src.kind == SCCPPass::LatticeKind::UNDEF) return makeUndef();
        if (src.kind != SCCPPass::LatticeKind::CONST) return makeOverdefined();

        return makeConstInt(src.type == DataType::F32 ? static_cast<int>(src.f32) : src.i32);
    }

    SCCPPass::LatticeVal SCCPEvalVisitor::evalPhi(SCCPPass& pass, PhiInst& inst, Block* block) const
    {
        if (!block) return makeUndef();
        SCCPPass::LatticeVal result      = makeUndef();
        bool                 hasIncoming = false;

        // 只合并可达前驱，避免不可达路径污染
        for (auto& [labelOp, valOp] : inst.incomingVals)
        {
            if (!labelOp || labelOp->getType() != OperandType::LABEL) continue;
            size_t predId = static_cast<LabelOperand*>(labelOp)->lnum;
            if (pass.reachableEdges.count({predId, block->blockId}) == 0) continue;
            hasIncoming = true;
            result      = mergeValue(result, getValue(pass, valOp));
            if (result.kind == SCCPPass::LatticeKind::OVERDEFINED) break;
        }

        if (!hasIncoming) return makeUndef();
        return result;
    }

    void SCCPEvalVisitor::visit(LoadInst& inst, SCCPPass& pass, Block* block)
    {
        // Load/Alloca/GEP 默认过定义，SCCP 不做内存求值
        updateValue(pass, inst.res, makeOverdefined());
    }
    void SCCPEvalVisitor::visit(StoreInst& inst, SCCPPass& pass, Block* block) { (void)inst; }
    void SCCPEvalVisitor::visit(ArithmeticInst& inst, SCCPPass& pass, Block* block)
    {
        // 常量算术折叠
        updateValue(pass, inst.res, evalArithmetic(pass, inst));
    }
    void SCCPEvalVisitor::visit(IcmpInst& inst, SCCPPass& pass, Block* block)
    {
        // 常量比较折叠
        updateValue(pass, inst.res, evalIcmp(pass, inst));
    }
    void SCCPEvalVisitor::visit(FcmpInst& inst, SCCPPass& pass, Block* block)
    {
        // 常量比较折叠（浮点）
        updateValue(pass, inst.res, evalFcmp(pass, inst));
    }
    void SCCPEvalVisitor::visit(AllocaInst& inst, SCCPPass& pass, Block* block)
    {
        // 内存分配结果不可确定
        updateValue(pass, inst.res, makeOverdefined());
    }
    void SCCPEvalVisitor::visit(BrCondInst& inst, SCCPPass& pass, Block* block)
    {
        if (!block) return;
        if (!inst.trueTar || !inst.falseTar) return;
        if (inst.trueTar->getType() != OperandType::LABEL || inst.falseTar->getType() != OperandType::LABEL) return;

        size_t trueId  = static_cast<LabelOperand*>(inst.trueTar)->lnum;
        size_t falseId = static_cast<LabelOperand*>(inst.falseTar)->lnum;

        // 分支条件若为常量可直接确定可达边
        auto condVal = getValue(pass, inst.cond);
        if (condVal.kind == SCCPPass::LatticeKind::CONST && condVal.type != DataType::F32)
        {
            if (condVal.i32 != 0) markEdgeReachable(pass, block->blockId, trueId);
            else markEdgeReachable(pass, block->blockId, falseId);
            return;
        }

        markEdgeReachable(pass, block->blockId, trueId);
        markEdgeReachable(pass, block->blockId, falseId);
    }
    void SCCPEvalVisitor::visit(BrUncondInst& inst, SCCPPass& pass, Block* block)
    {
        if (!block) return;
        if (!inst.target || inst.target->getType() != OperandType::LABEL) return;
        size_t targetId = static_cast<LabelOperand*>(inst.target)->lnum;
        markEdgeReachable(pass, block->blockId, targetId);
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
        // 常量类型转换折叠
        updateValue(pass, inst.dest, evalFP2SI(pass, inst));
    }
    void SCCPEvalVisitor::visit(SI2FPInst& inst, SCCPPass& pass, Block* block)
    {
        // 常量类型转换折叠
        updateValue(pass, inst.dest, evalSI2FP(pass, inst));
    }
    void SCCPEvalVisitor::visit(ZextInst& inst, SCCPPass& pass, Block* block)
    {
        // 常量类型转换折叠
        updateValue(pass, inst.dest, evalZext(pass, inst));
    }
    void SCCPEvalVisitor::visit(PhiInst& inst, SCCPPass& pass, Block* block)
    {
        // Phi 只合并可达前驱的格值
        updateValue(pass, inst.res, evalPhi(pass, inst, block));
    }

    // 把寄存器操作数替换为对应常量（若已知）
    void SCCPReplaceVisitor::replaceOperandIfConst(SCCPPass& pass, Operand*& op)
    {
        if (!op || op->getType() != OperandType::REG) return;
        auto* reg = static_cast<RegOperand*>(op);
        auto  it  = pass.valueMap.find(reg->regNum);
        if (it == pass.valueMap.end()) return;
        const auto& val = it->second;
        Operand* constOp = nullptr;
        if (val.kind == SCCPPass::LatticeKind::CONST)
        {
            if (val.type == DataType::F32) constOp = getImmeF32Operand(val.f32);
            else constOp = getImmeI32Operand(val.i32);
        }
        if (!constOp) return;
        op = constOp;
    }

    void SCCPReplaceVisitor::visit(LoadInst& inst, SCCPPass& pass) { replaceOperandIfConst(pass, inst.ptr); }
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
    void SCCPReplaceVisitor::visit(BrCondInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.cond);
    }
    void SCCPReplaceVisitor::visit(BrUncondInst& inst, SCCPPass& pass) { (void)inst; }
    void SCCPReplaceVisitor::visit(GlbVarDeclInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.init);
    }
    void SCCPReplaceVisitor::visit(CallInst& inst, SCCPPass& pass)
    {
        for (auto& arg : inst.args) replaceOperandIfConst(pass, arg.second);
    }
    void SCCPReplaceVisitor::visit(FuncDeclInst& inst, SCCPPass& pass) { (void)inst; }
    void SCCPReplaceVisitor::visit(FuncDefInst& inst, SCCPPass& pass) { (void)inst; }
    void SCCPReplaceVisitor::visit(RetInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.res);
    }
    void SCCPReplaceVisitor::visit(GEPInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.basePtr);
        for (auto& idx : inst.idxs) replaceOperandIfConst(pass, idx);
    }
    void SCCPReplaceVisitor::visit(FP2SIInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.src);
    }
    void SCCPReplaceVisitor::visit(SI2FPInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.src);
    }
    void SCCPReplaceVisitor::visit(ZextInst& inst, SCCPPass& pass)
    {
        replaceOperandIfConst(pass, inst.src);
    }
    void SCCPReplaceVisitor::visit(PhiInst& inst, SCCPPass& pass)
    {
        for (auto& [label, val] : inst.incomingVals) { replaceOperandIfConst(pass, val); }
    }

}  // namespace ME
