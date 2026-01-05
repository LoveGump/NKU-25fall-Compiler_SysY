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

        // 收集已使用的被调用者保存寄存器
        const std::vector<int> calleeSavedInt = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
        const std::vector<int> calleeSavedFP  = {40, 41, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59};

        // 1.计算栈大小与基准偏移
        // 统一的帧基准偏移，保证调用者/被调用者对参数区的认知一致
        // ra和保存的寄存器区 + 对齐填充
        int maxSaveArea = 8 + static_cast<int>(calleeSavedInt.size() + calleeSavedFP.size()) * 8;
        int baseOffset  = (maxSaveArea + 15) & ~15;  // 16 字节对齐
        // 设置基准偏移
        func->frameInfo.setBaseOffset(baseOffset);

        // 计算总栈大小
        int frameSize = func->frameInfo.calculateOffsets();
        int stackSize = baseOffset + frameSize;
        stackSize     = (stackSize + 15) & ~15;
        // 设置函数栈大小
        func->stackSize = stackSize;

        // 2. 展开 FrameIndexOperand
        // 对应的指令，偏移量 = 对应对象偏移 + 指令中偏移量
        auto replaceLargeOffsetWithT0 = [&](Instr* inst, int offset, BE::Block* block,
                                            std::deque<MInstruction*>::iterator& it) {
            // 使用 t0 实现大偏移：LI t0, offset; ADDI/ADD base, sp, t0; 原指令 offset=0 base=t0
            auto* li  = createUInst(Operator::LI, PR::t0, offset);
            auto* add = createRInst(Operator::ADD, PR::t0, PR::sp, PR::t0);

            // 存储治理
            bool isStore = inst->op == Operator::SW || inst->op == Operator::SD || 
                           inst->op == Operator::FSW || inst->op == Operator::FSD;
            if (isStore)
                inst->rs2 = PR::t0;  // 存的基地址 rs2
            else
                inst->rs1 = PR::t0;  // 其余指令基地址 rs1

            // 清理偏移和帧索引操作数
            inst->imme = 0;
            inst->use_ops = false;
            delete inst->fiop;
            inst->fiop = nullptr;

            // 插入新指令
            it = block->insts.insert(it, li);
            ++it;
            it = block->insts.insert(it, add);
            ++it;  // it 指向原指令下一条
        };

        for (auto& [bid, block] : func->blocks)
        {
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                // 遍历指令列表
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;

                auto* rv64Inst = static_cast<Instr*>(inst);

                // 如果使用了帧索引操作数，展开
                if (rv64Inst->use_ops && rv64Inst->fiop && rv64Inst->fiop->ot == Operand::Type::FRAME_INDEX)
                {
                    // 获取帧索引操作数
                    auto* fiOp  = static_cast<FrameIndexOperand*>(rv64Inst->fiop);
                    int   baseOffset = func->frameInfo.getObjectOffset(fiOp->frameIndex);
                    if (baseOffset >= 0)
                    {
                        int totalOffset = baseOffset + rv64Inst->imme;
                        if (totalOffset < -2048 || totalOffset > 2047)
                        {
                            // 超过立即数范围，使用 t0 计算地址
                            replaceLargeOffsetWithT0(rv64Inst, totalOffset, block, it);
                        }
                        else
                        {
                            // 直接使用立即数偏移
                            rv64Inst->imme    = totalOffset;
                            rv64Inst->use_ops = false;
                            delete rv64Inst->fiop;
                            rv64Inst->fiop = nullptr;
                        }
                    }
                }
            }
        }

        // 2.修正参数栈 加载偏移
        // 参数栈位置相对于调用者的 sp，需加上当前函数的 stackSize
        if (!func->blocks.empty())
        {
            BE::Block* entryBlock = func->blocks.begin()->second;
            for (auto it = entryBlock->insts.begin(); it != entryBlock->insts.end(); ++it)
            {
                // 遍历指令列表
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;
                auto* rv64Inst = static_cast<Instr*>(inst);
                // 如果步是参数栈加载指令，跳过
                if (rv64Inst->comment != "param_stack") continue;

                // 计算总偏移 = 指令偏移 + 函数栈大小 + 基地址偏移
                int totalOffset = rv64Inst->imme + stackSize + baseOffset;
                if (totalOffset >= -2048 && totalOffset <= 2047)
                {
                    // 直接使用立即数偏移
                    rv64Inst->imme = totalOffset;
                    rv64Inst->comment.clear();
                }
                else
                {
                    // 如果在立即数范围外，使用 t0 计算地址
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

        // 3.修正调用栈参数存储偏移
        // 传出参数区域从 baseOffset 开始
        for (auto& [bid, block] : func->blocks)
        {
            for (auto it = block->insts.begin(); it != block->insts.end(); ++it)
            {
                // 遍历指令列表
                auto* inst = *it;
                if (!inst || inst->kind != InstKind::TARGET) continue;
                auto* rv64Inst = static_cast<Instr*>(inst);
                if (rv64Inst->comment != "call_stackarg") continue;

                // 计算总偏移 = 指令偏移 + 基地址偏移
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

    }

}  // namespace BE::RV64::Passes::Lowering
