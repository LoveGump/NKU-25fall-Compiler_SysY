#include "backend/targets/riscv64/rv64_defs.h"
#include <backend/targets/riscv64/passes/lowering/frame_lowering.h>
#include <debug.h>
#include <algorithm>
#include <map>
#include <vector>
#include <deque>

namespace BE::RV64::Passes::Lowering
{
    using namespace BE;
    using namespace BE::RV64;

    void FrameLoweringPass::runOnModule(BE::Module& module)
    {
        for (auto* func : module.functions) runOnFunction(func);
    }

    void FrameLoweringPass::runOnFunction(BE::Function* func)
    {
        if (!func || func->blocks.empty()) return;

        // ====================== 收集已使用的被调用者保存寄存器 ======================
        const std::vector<int> calleeSavedInt = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
        const std::vector<int> calleeSavedFP  = {40, 41, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59};

        // Conservatively save all integer callee-saved registers. Some later
        // lowering passes may introduce additional uses that are invisible when
        // we scan here (post-RA but pre-stack-lowering), which can lead to
        // unsaved registers like s2 being clobbered. Saving the full set keeps
        // the ABI contract intact at the cost of a small stack increase.
        std::vector<int> usedCSInt = calleeSavedInt;
        std::vector<int> usedCSFP;

        bool usesFloatReg = false;

        auto recordUse = [&](const Register& r) {
            if (r.dt && r.dt->dt == BE::DataType::Type::FLOAT) usesFloatReg = true;
            // Some physical registers may still carry the isVreg flag after lowering,
            // so rely on the register id instead of the flag when collecting
            // callee-saved usage.
            if (std::find(calleeSavedFP.begin(), calleeSavedFP.end(), static_cast<int>(r.rId)) !=
                calleeSavedFP.end())
                usedCSFP.push_back(r.rId);
        };

        for (auto& [bid, block] : func->blocks)
        {
            for (auto* inst : block->insts)
            {
                if (inst->kind != InstKind::TARGET) continue;
                auto* ri = static_cast<Instr*>(inst);
                recordUse(ri->rd);
                recordUse(ri->rs1);
                recordUse(ri->rs2);
            }
        }

        // Ensure fs0 is preserved for any function that touches floating-point registers,
        // even if register flags were not lowered cleanly.
        if (usesFloatReg &&
            std::find(usedCSFP.begin(), usedCSFP.end(), static_cast<int>(PR::fs0.rId)) == usedCSFP.end())
            usedCSFP.push_back(static_cast<int>(PR::fs0.rId));
        // Some callees allocate other fs* registers even when they don't explicitly appear
        // in the lowered operands (e.g., due to missed type tags). Preserve the full
        // callee-saved FP set whenever the function touches floating-point registers to
        // avoid clobbering caller state.
        if (usesFloatReg) usedCSFP.insert(usedCSFP.end(), calleeSavedFP.begin(), calleeSavedFP.end());

        auto uniqueRegs = [](std::vector<int>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        uniqueRegs(usedCSInt);
        uniqueRegs(usedCSFP);
        std::cerr << "[FrameLowering] function " << func->name << " uses callee-saved int=" << usedCSInt.size()
                  << " fp=" << usedCSFP.size() << std::endl;

        // ====================== 计算栈大小与基准偏移 ======================
        // 统一的帧基准偏移，保证调用者/被调用者对参数区的认知一致
        int maxSaveArea = 8 + static_cast<int>(calleeSavedInt.size() + calleeSavedFP.size()) * 8;
        int baseOffset  = (maxSaveArea + 15) & ~15;  // 16 字节对齐
        func->frameInfo.setBaseOffset(baseOffset);

        int frameSize = func->frameInfo.calculateOffsets();
        int stackSize = baseOffset + frameSize;
        stackSize     = (stackSize + 15) & ~15;
        func->stackSize = stackSize;

        // ====================== 展开 FrameIndexOperand ======================
        auto replaceLargeOffsetWithT0 = [&](Instr* inst, int offset, BE::Block* block,
                                            std::deque<MInstruction*>::iterator& it) {
            // 使用 t0 实现大偏移：LI t0, offset; ADDI/ADD base, sp, t0; 原指令 offset=0 base=t0
            auto* li  = createUInst(Operator::LI, PR::t0, offset);
            auto* add = createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0);
            bool isStore =
                inst->op == Operator::SW || inst->op == Operator::SD || inst->op == Operator::FSW ||
                inst->op == Operator::FSD;
            if (isStore)
                inst->rs2 = PR::t0;  // store base is rs2
            else
                inst->rs1 = PR::t0;  // load/other base is rs1
            inst->imme = 0;
            inst->use_ops = false;
            delete inst->fiop;
            inst->fiop = nullptr;

            it = block->insts.insert(it, li);
            ++it;
            it = block->insts.insert(it, add);
            ++it;  // it now points to original inst position (before increment in caller loop)
        };

        for (auto& [bid, block] : func->blocks)
        {
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;

                auto* rv64Inst = static_cast<Instr*>(inst);

                if (rv64Inst->use_ops && rv64Inst->fiop && rv64Inst->fiop->ot == Operand::Type::FRAME_INDEX)
                {
                    auto* fiOp  = static_cast<FrameIndexOperand*>(rv64Inst->fiop);
                    int   baseOffset = func->frameInfo.getObjectOffset(fiOp->frameIndex);
                    if (baseOffset >= 0)
                    {
                        int totalOffset = baseOffset + rv64Inst->imme;
                        if (totalOffset < -2048 || totalOffset > 2047)
                        {
                            replaceLargeOffsetWithT0(rv64Inst, totalOffset, block, it);
                        }
                        else
                        {
                            rv64Inst->imme    = totalOffset;
                            rv64Inst->use_ops = false;
                            delete rv64Inst->fiop;
                            rv64Inst->fiop = nullptr;
                        }
                    }
                }
            }
        }

        // ====================== 修正参数栈加载偏移 ======================
        // 参数栈位置相对于调用者的 sp，需加上当前函数的 stackSize
        if (!func->blocks.empty())
        {
            BE::Block* entryBlock = func->blocks.begin()->second;
            for (auto it = entryBlock->insts.begin(); it != entryBlock->insts.end(); ++it)
            {
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;
                auto* rv64Inst = static_cast<Instr*>(inst);
                if (rv64Inst->comment != "param_stack") continue;

                int totalOffset = rv64Inst->imme + stackSize + baseOffset;
                if (totalOffset >= -2048 && totalOffset <= 2047)
                {
                    rv64Inst->imme = totalOffset;
                    rv64Inst->comment.clear();
                }
                else
                {
                    auto* li  = createUInst(Operator::LI, PR::t0, totalOffset);
                    auto* add = createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0);
                    rv64Inst->rs1  = PR::t0;
                    rv64Inst->imme = 0;
                    rv64Inst->comment.clear();
                    it             = entryBlock->insts.insert(it, li);
                    ++it;
                    it = entryBlock->insts.insert(it, add);
                }
            }
        }

        // ====================== 修正调用栈参数存储偏移 ======================
        // 传出参数区域从 baseOffset 开始
        for (auto& [bid, block] : func->blocks)
        {
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;
                auto* rv64Inst = static_cast<Instr*>(inst);
                if (rv64Inst->comment != "call_stackarg") continue;

                int totalOffset = rv64Inst->imme + baseOffset;
                if (totalOffset >= -2048 && totalOffset <= 2047)
                {
                    rv64Inst->imme = totalOffset;
                    rv64Inst->comment.clear();
                }
                else
                {
                    auto* li  = createUInst(Operator::LI, PR::t0, totalOffset);
                    auto* add = createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0);
                    bool isStore =
                        rv64Inst->op == Operator::SW || rv64Inst->op == Operator::SD || rv64Inst->op == Operator::FSW ||
                        rv64Inst->op == Operator::FSD;
                    if (isStore)
                        rv64Inst->rs2 = PR::t0;
                    else
                        rv64Inst->rs1 = PR::t0;
                    rv64Inst->imme = 0;
                    rv64Inst->comment.clear();
                    it             = block->insts.insert(it, li);
                    ++it;
                    it = block->insts.insert(it, add);
                }
            }
        }

        // 如果栈大小为 0，则不需要 prologue/epilogue
        if (stackSize == 0) return;

        // 找到入口块（假设第一个块是入口）
        BE::Block* entryBlock = nullptr;
        for (auto& [id, block] : func->blocks)
        {
            entryBlock = block;
            break;
        }

        // 保存寄存器的偏移映射
        std::map<int, int> saveOffsets;
        int                curSaveOff = 0;
        saveOffsets[PR::ra.rId] = curSaveOff;
        curSaveOff += 8;
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

        // ========== 生成 Prologue ==========
        if (entryBlock)
        {
            std::deque<MInstruction*> prologue;

            // ADDI sp, sp, -stackSize
            if (stackSize >= -2048 && stackSize <= 2047)
                prologue.push_back(createIInst(Operator::ADDI, PR::sp, PR::sp, -stackSize));
            else
            {
                prologue.push_back(createUInst(Operator::LI, PR::t0, -stackSize));
                prologue.push_back(createRInst(Operator::ADD, PR::sp, PR::sp, PR::t0));
            }

            // 保存 ra
            prologue.push_back(createSInst(Operator::SD, PR::ra, PR::sp, saveOffsets[PR::ra.rId]));
            // 保存被调用者保存寄存器
            for (int r : usedCSInt) prologue.push_back(createSInst(Operator::SD, Register(r), PR::sp, saveOffsets[r]));
            for (int r : usedCSFP)
            {
                auto* inst = createSInst(Operator::FSD, Register(r, BE::F64), PR::sp, saveOffsets[r]);
                prologue.push_back(inst);
            }

            for (auto it = prologue.rbegin(); it != prologue.rend(); ++it)
            {
                entryBlock->insts.push_front(*it);
            }
        }

        // ========== 生成 Epilogue ==========
        for (auto& [id, block] : func->blocks)
        {
            if (!block) continue;

            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;

                auto* rv64Inst = static_cast<Instr*>(inst);

                bool isRetInst = false;
                if (rv64Inst->op == Operator::JALR && rv64Inst->rd.rId == PR::x0.rId && rv64Inst->rs1.rId == PR::ra.rId &&
                    rv64Inst->imme == 0)
                    isRetInst = true;
                if (rv64Inst->op == Operator::RET) isRetInst = true;

                if (isRetInst)
                {
                    std::vector<MInstruction*> epilogue;

                    // 恢复寄存器（逆序）
                    for (auto rit = usedCSFP.rbegin(); rit != usedCSFP.rend(); ++rit)
                        epilogue.push_back(createIInst(Operator::FLD, Register(*rit, BE::F64), PR::sp, saveOffsets[*rit]));
                    for (auto rit = usedCSInt.rbegin(); rit != usedCSInt.rend(); ++rit)
                        epilogue.push_back(createIInst(Operator::LD, Register(*rit), PR::sp, saveOffsets[*rit]));
                    epilogue.push_back(createIInst(Operator::LD, PR::ra, PR::sp, saveOffsets[PR::ra.rId]));

                    // 恢复 sp
                    if (stackSize >= -2048 && stackSize <= 2047)
                        epilogue.push_back(createIInst(Operator::ADDI, PR::sp, PR::sp, stackSize));
                    else
                    {
                        epilogue.push_back(createUInst(Operator::LI, PR::t0, stackSize));
                        epilogue.push_back(createRInst(Operator::ADD, PR::sp, PR::sp, PR::t0));
                    }

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
