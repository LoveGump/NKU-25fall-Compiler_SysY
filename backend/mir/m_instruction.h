#ifndef __BACKEND_MIR_M_INSTRUCTION_H__
#define __BACKEND_MIR_M_INSTRUCTION_H__

/**
 * @file m_instruction.h
 * @brief 后端机器指令定义
 *
 * 定义了后端的机器指令基类（MInstruction）和各种伪指令：
 * - NopInst: 空指令（可用于占位或注释）
 * - PhiInst: Φ 指令（SSA 形式的控制流合并）
 * - MoveInst: 数据移动指令（寄存器到寄存器/立即数）
 * - FILoadInst/FIStoreInst: 栈槽加载/存储指令（用于溢出处理）
 *
 * 目标相关的具体指令（如 RISC-V 的 ADD/LW/SW 等）在 targets/xxx/xxx_defs.h 中定义。
 */

#include <backend/mir/m_defs.h>
#include <map>

namespace BE
{
    /**
     * @brief 机器指令基类
     *
     * 所有后端指令的基类，存储指令类型（kind）和可选的注释。
     * 目标相关指令（InstKind::TARGET）由各架构自行扩展。
     */
    class MInstruction
    {
      public:
        InstKind    kind;     ///< 指令类型（NOP/PHI/MOVE/TARGET 等）
        std::string comment;  ///< 可选的注释信息
        uint32_t    id;       ///< 指令 ID（用于寄存器分配等）

      public:
        static void delInst(MInstruction* inst)
        {
            if (!inst) return;
            delete inst;
            inst = nullptr;
        }

      protected:
        MInstruction(InstKind k, const std::string& c = "") : kind(k), comment(c) {}
        virtual ~MInstruction() = default;
    };

    /**
     * @brief 伪指令基类
     *
     * 伪指令是目标无关的指令，在后续 Pass 中会被展开为目标相关的实际指令。
     */
    class PseudoInst : public MInstruction
    {
      public:
        PseudoInst(InstKind k, const std::string& c = "") : MInstruction(k, c) {}
    };

    /// @brief 空指令，可用于占位或作为注释载体
    class NopInst : public PseudoInst
    {
      public:
        NopInst(const std::string& c = "") : PseudoInst(InstKind::NOP, c) {}
    };

    /**
     * @brief Φ 指令（SSA 形式的控制流合并）
     *
     * 根据前驱块选择不同的值，在后续的 PhiElimination Pass 中会被消解为
     * 在各前驱块末尾插入的 MOVE 指令。
     */
    class PhiInst : public PseudoInst
    {
      public:
        using labelId = uint32_t;
        using srcOp   = Operand*;
        std::map<labelId, srcOp> incomingVals;  ///< 前驱块 ID -> 对应的值
        Register                 resReg;        ///< 结果寄存器

      public:
        PhiInst(Register res, const std::string& c = "") : PseudoInst(InstKind::PHI, c), resReg(res) {}
    };

    /**
     * @brief 数据移动指令
     *
     * 寄存器到寄存器或立即数到寄存器的数据拷贝。
     * 在后续 Pass 中会被展开为目标相关的 MOV/LI 指令。
     */
    class MoveInst : public PseudoInst
    {
      public:
        Operand* src;   ///< 源操作数（寄存器或立即数）
        Operand* dest;  ///< 目标操作数（寄存器）

      public:
        MoveInst(Operand* s, Operand* d, const std::string& c = "") : PseudoInst(InstKind::MOVE, c), src(s), dest(d) {}
    };

    /**
     * @brief 栈槽加载指令（用于溢出处理）
     *
     * 从 frameIndex 指定的栈槽加载数据到 dest 寄存器。
     * 由寄存器分配器插入，在 StackLowering Pass 中展开为实际的 LOAD 指令。
     */
    class FILoadInst : public PseudoInst
    {
      public:
        Register dest;        ///< 目标物理寄存器
        int      frameIndex;  ///< MFrameInfo 中的溢出槽索引

      public:
        FILoadInst(Register d, int fi, const std::string& c = "")
            : PseudoInst(InstKind::LSLOT, c), dest(d), frameIndex(fi)
        {}
    };

    /**
     * @brief 栈槽存储指令（用于溢出处理）
     *
     * 将 src 寄存器的数据存储到 frameIndex 指定的栈槽。
     * 由寄存器分配器插入，在 StackLowering Pass 中展开为实际的 STORE 指令。
     */
    class FIStoreInst : public PseudoInst
    {
      public:
        Register src;         ///< 源物理寄存器
        int      frameIndex;  ///< MFrameInfo 中的溢出槽索引

      public:
        FIStoreInst(Register s, int fi, const std::string& c = "")
            : PseudoInst(InstKind::SSLOT, c), src(s), frameIndex(fi)
        {}
    };

    MoveInst* createMove(Operand* dst, Operand* src, const std::string& c = "");
    MoveInst* createMove(Operand* dst, int imme, const std::string& c = "");
    MoveInst* createMove(Operand* dst, float imme, const std::string& c = "");
}  // namespace BE

#endif  // __BACKEND_MIR_M_INSTRUCTION_H__
