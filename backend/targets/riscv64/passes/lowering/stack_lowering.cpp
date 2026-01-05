#include <backend/targets/riscv64/passes/lowering/stack_lowering.h>
#include <backend/mir/m_function.h>
#include <backend/mir/m_instruction.h>
#include <backend/mir/m_defs.h>
#include <backend/targets/riscv64/rv64_defs.h>
#include <backend/targets/riscv64/rv64_reg_info.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <cstring>

namespace BE::RV64::Passes::Lowering
{
    using namespace BE;
    using namespace BE::RV64;

    void StackLoweringPass::runOnModule(BE::Module& module)
    {
        for (auto* func : module.functions) lowerFunction(func);
    }

    // 扩展栈溢出槽相关伪指令和 MOVE 指令
    void StackLoweringPass::lowerFunction(BE::Function* func)
    {
        if (!func || func->blocks.empty()) return;
        // 扩展溢出槽相关伪指令
        for (auto& [bid, block] : func->blocks)
        {
            // 遍历所有的指令，寻找 LSLOT 和 SSLOT 指令
            std::deque<MInstruction*> newInsts;
            for (auto* inst : block->insts)
            {
                // 遍历所有指令
                if (!inst)
                    continue; 

                if (inst->kind == InstKind::LSLOT)
                {
                    // 如果是 LSLOT 指令，进行扩展，据类型选择指令：LW/LD 或 FLW/FLD
                    auto* fiLoad = static_cast<FILoadInst*>(inst);
                    // 获取对应的栈偏移
                    int   offset = func->frameInfo.getSpillSlotOffset(fiLoad->frameIndex);

                    // emitLoad 创建加载指令
                    // base 是基址寄存器，off 是偏移量
                    auto emitLoad = [&](Register base, int off) -> MInstruction* {
                        // 判断是否为浮点类型和32位类型
                        bool isFloat = fiLoad->dest.dt && fiLoad->dest.dt->dt == DataType::Type::FLOAT;
                        bool is32    = fiLoad->dest.dt && fiLoad->dest.dt->dl == DataType::Length::B32;
                        Operator op;
                        // 如果是浮点类型
                        if (isFloat)
                            op = is32 ? Operator::FLW : Operator::FLD;
                        else
                            op = is32 ? Operator::LW : Operator::LD;

                        // 创建加载指令
                        return createIInst(op, fiLoad->dest, base, off);
                    };

                    // 处理偏移量超出立即数范围的情况
                    if (offset < -2048 || offset > 2047)
                    {
                        // 创建临时寄存器 t0 来存储偏移地址
                        newInsts.push_back(createUInst(Operator::LI, PR::t0, offset));
                        newInsts.push_back(createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0));
                        // 使用临时寄存器进行加载
                        newInsts.push_back(emitLoad(PR::t0, 0));
                    }
                    else
                    {
                        // 直接使用栈指针和偏移量进行加载
                        newInsts.push_back(emitLoad(PR::sp, offset));
                    }

                    delete fiLoad;
                    continue;
                }
                else if (inst->kind == InstKind::SSLOT)
                {
                    // 如果是 SSLOT 指令，进行扩展，根据类型选择指令：SW/SD 或 FSW/FSD
                    auto* fiStore = static_cast<FIStoreInst*>(inst);
                    // 获取对应的栈偏移
                    int   offset  = func->frameInfo.getSpillSlotOffset(fiStore->frameIndex);

                    // emitStore 创建存储指令
                    // base 是基址寄存器，off 是偏移量
                    auto emitStore = [&](Register base, int off) -> MInstruction* {
                        bool isFloat = fiStore->src.dt && fiStore->src.dt->dt == DataType::Type::FLOAT;
                        bool is32    = fiStore->src.dt && fiStore->src.dt->dl == DataType::Length::B32;
                        Operator op;
                        if (isFloat)
                            op = is32 ? Operator::FSW : Operator::FSD;
                        else
                            op = is32 ? Operator::SW : Operator::SD;
                        return createSInst(op, fiStore->src, base, off);
                    };

                    if (offset < -2048 || offset > 2047)
                    {
                        newInsts.push_back(createUInst(Operator::LI, PR::t0, offset));
                        newInsts.push_back(createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0));
                        newInsts.push_back(emitStore(PR::t0, 0));
                    }
                    else
                    {
                        newInsts.push_back(emitStore(PR::sp, offset));
                    }

                    delete fiStore;
                    continue;
                }

                newInsts.push_back(inst);
            }
            block->insts.swap(newInsts);
        }

        // 扩展 MOVE 指令
        for (auto& [bid, block] : func->blocks)
        {
            std::deque<MInstruction*> newInsts;
            for (auto* inst : block->insts)
            {
                if (!inst)
                    continue;

                // move 指令扩展
                if (inst->kind == InstKind::MOVE)
                {
                    auto* mvInst = static_cast<MoveInst*>(inst);

                    // 获取目标操作数寄存器
                    auto* dstOp  = dynamic_cast<RegOperand*>(mvInst->dest);
                    if (!dstOp)
                    {
                        delete mvInst;
                        continue;
                    }

                    // 获取目标寄存器
                    Register destReg = dstOp->reg;
                    if (destReg.isVreg)
                    {
                        delete mvInst;
                        continue;
                    }

                    // 创建替换指令
                    MInstruction* replacement = nullptr;
                    if (auto* srcRegOp = dynamic_cast<RegOperand*>(mvInst->src))
                    {
                        // 源操作数是寄存器
                        if (destReg.dt && destReg.dt->dt == DataType::Type::FLOAT)
                            // 如果是浮点类型，使用浮点移动指令
                            replacement = createR2Inst(Operator::FMV_S, destReg, srcRegOp->reg);
                        else
                            // 否则使用整数移动指令
                            replacement = createIInst(Operator::ADDI, destReg, srcRegOp->reg, 0);
                    }
                    else if (auto* immOp = dynamic_cast<I32Operand*>(mvInst->src))
                    {
                        // 源操作数是整数立即数
                        replacement = createUInst(Operator::LI, destReg, immOp->val);
                    }
                    else if (auto* fImmOp = dynamic_cast<F32Operand*>(mvInst->src))
                    {
                        // 源操作数是浮点立即数
                        // RISCV没有直接加载浮点立即数的指令，需要先加载到整数寄存器再转换
                        // 1.将浮点数的位模式作为整数加载到临时寄存器 t0
                        // 2.将整数寄存器的值移动到目标浮点寄存器
                        uint32_t bits = 0;
                        // static_assert(sizeof(bits) == sizeof(float), "size mismatch");
                        // 将浮点数的位模式复制到整数变量 bits 中
                        std::memcpy(&bits, &fImmOp->val, sizeof(float));
                        // 加载到临时寄存器 t0
                        newInsts.push_back(createUInst(Operator::LI, PR::t0, static_cast<int>(bits)));

                        // 将t0的值移动到目标浮点寄存器
                        replacement = createR2Inst(Operator::FMV_W_X, destReg, PR::t0);
                    }

                    if (replacement) newInsts.push_back(replacement);
                    delete mvInst;
                    continue;
                }

                newInsts.push_back(inst);
            }
            block->insts.swap(newInsts);
        }

        // 收集已使用的被调用者保存寄存器
        const std::vector<int> calleeSavedInt = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
        const std::vector<int> calleeSavedFP  = {40, 41, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59};

        // 保守地保存所有整数被调用者保存寄存器。
        std::vector<int> usedCSInt = calleeSavedInt;
        std::vector<int> usedCSFP;

        // 检测函数中是否使用了浮点寄存器
        bool usesFloatReg = false;

        // 记录使用的寄存器
        auto recordUse = [&](const Register& r) {
            // 如果是浮点寄存器，标记函数使用了浮点寄存器
            if (r.dt && r.dt->dt == DataType::Type::FLOAT) usesFloatReg = true;
            // 有些物理寄存器在 lowering 后仍可能带着 isVreg 标记，因此统计被调用者保存寄存器时
            // 以寄存器 id 为准，而不是标记位。
            /// 加入保存列表
            if (std::find(calleeSavedFP.begin(), calleeSavedFP.end(), static_cast<int>(r.rId)) !=
                calleeSavedFP.end())
                usedCSFP.push_back(r.rId);
        };

        for (auto& [bid, block] : func->blocks)
        {
            for (auto* inst : block->insts)
            {
                // 如果是目标指令，检查其操作数
                if (inst->kind != InstKind::TARGET) continue;
                auto* ri = static_cast<Instr*>(inst);
                recordUse(ri->rd);
                recordUse(ri->rs1);
                recordUse(ri->rs2);
            }
        }

        // 只要函数使用了浮点寄存器，就确保保存 fs0，即使寄存器标记没有正确清理。
        // 防止破坏调用者状态。
        if (usesFloatReg &&
            std::find(usedCSFP.begin(), usedCSFP.end(), static_cast<int>(PR::fs0.rId)) == usedCSFP.end())
            usedCSFP.push_back(static_cast<int>(PR::fs0.rId));
        
            // 将使用的浮点寄存器加入保存列表
        if (usesFloatReg) usedCSFP.insert(usedCSFP.end(), calleeSavedFP.begin(), calleeSavedFP.end());

        // 去重
        auto uniqueRegs = [](std::vector<int>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        // uniqueRegs(usedCSInt);
        uniqueRegs(usedCSFP);

        // 如果栈大小为 0，则不需要 prologue/epilogue
        if (func->stackSize == 0) return;

        // 找到入口块
        BE::Block* entryBlock = nullptr;
        if (!func->blocks.empty())
            entryBlock = func->blocks.begin()->second;

        // 保存寄存器的偏移映射 物理寄存器 id -> 偏移
        std::map<int, int> saveOffsets;
        // 先计算保存区偏移
        int                curSaveOff = 0;
        saveOffsets[PR::ra.rId] = curSaveOff;
        curSaveOff += 8;

        // 为每个寄存器分配保存偏移
        for (int r : usedCSInt)
        {
            saveOffsets[r] = curSaveOff;
            curSaveOff += 8;
        }
        for (int r : usedCSFP)
        {
            saveOffsets[r] = curSaveOff;
            curSaveOff += 8;
        }

        // 1.生成 Prologue：前研 保存fp ra sp 被保存的寄存器
        if (entryBlock)
        {
            std::deque<MInstruction*> prologue;

            // 调整 sp
            if (func->stackSize >= -2048 && func->stackSize <= 2047)
                // 直接使用 ADDI 指令调整栈指针
                prologue.push_back(createIInst(Operator::ADDI, PR::sp, PR::sp, -func->stackSize));
            else
            {
                // 使用 LI 和 ADD 指令调整栈指针
                prologue.push_back(createUInst(Operator::LI, PR::t0, -func->stackSize));
                prologue.push_back(createRInst(Operator::ADD, PR::sp, PR::sp, PR::t0));
            }

            // 保存 ra
            prologue.push_back(createSInst(Operator::SD, PR::ra, PR::sp, saveOffsets[PR::ra.rId]));
            // 保存被调用者保存寄存器
            for (int r : usedCSInt) {
                // 保存整数寄存器
                prologue.push_back(createSInst(Operator::SD, Register(r), PR::sp, saveOffsets[r]));
            }
            for (int r : usedCSFP)
            {
                // 保存浮点寄存器
                auto* inst = createSInst(Operator::FSD, Register(r, BE::F64), PR::sp, saveOffsets[r]);
                prologue.push_back(inst);
            }

            // 插入到入口块开头，倒序插入
            for (auto it = prologue.rbegin(); it != prologue.rend(); ++it)
            {
                entryBlock->insts.push_front(*it);
            }
        }

        // 2. 生成 Epilogue：恢复 fp ra sp 被保存的寄存器
        for (auto& [id, block] : func->blocks)
        {
            if (!block) continue;

            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                // 遍历指令列表
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;

                auto* rv64Inst = static_cast<Instr*>(inst);

                bool isRetInst = false;

                // 检测是否为返回指令
                if (rv64Inst->op == Operator::JALR && rv64Inst->rd.rId == PR::x0.rId && rv64Inst->rs1.rId == PR::ra.rId &&
                    rv64Inst->imme == 0)
                    isRetInst = true;
                if (rv64Inst->op == Operator::RET) isRetInst = true;

                if (isRetInst)
                {
                    // 如果是返回指令，插入恢复指令序列
                    std::vector<MInstruction*> epilogue;

                    // 恢复寄存器（逆序）
                    for (auto rit = usedCSFP.rbegin(); rit != usedCSFP.rend(); ++rit)
                        epilogue.push_back(createIInst(Operator::FLD, Register(*rit, BE::F64), PR::sp, saveOffsets[*rit]));
                    for (auto rit = usedCSInt.rbegin(); rit != usedCSInt.rend(); ++rit)
                        epilogue.push_back(createIInst(Operator::LD, Register(*rit), PR::sp, saveOffsets[*rit]));
                    epilogue.push_back(createIInst(Operator::LD, PR::ra, PR::sp, saveOffsets[PR::ra.rId]));

                    // 恢复 sp
                    if (func->stackSize >= -2048 && func->stackSize <= 2047)
                        epilogue.push_back(createIInst(Operator::ADDI, PR::sp, PR::sp, func->stackSize));
                    else
                    {
                        epilogue.push_back(createUInst(Operator::LI, PR::t0, func->stackSize));
                        epilogue.push_back(createRInst(Operator::ADD, PR::sp, PR::sp, PR::t0));
                    }

                    // 插入恢复指令序列
                    for (auto* eInst : epilogue)
                    {
                        it = block->insts.insert(it, eInst);
                        ++it;
                    }

                    break;  // 一个块只应该有一个 RET
                }
            }
        }
    }
}  // namespace BE::RV64::Passes::Lowering
