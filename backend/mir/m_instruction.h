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
        InstKind    kind;     ///< 指令类型：标识指令具体类别；影响后续指令选择与发射；决定了指令在流水线中的基本行为。
        std::string comment;  ///< 调试注释：存储可读性信息；不影响代码生成逻辑；在汇编输出中作为注释行出现。
        uint32_t    id;       ///< 指令 ID：唯一标识符；用于活跃分析和寄存器分配中的索引；在指令序列中提供拓扑参考。

      public:
        /**
         * @brief 销毁指令对象
         * 
         * 静态辅助函数，用于安全释放指令内存。
         * @param inst 指向待销毁指令的指针
         */
        static void delInst(MInstruction* inst)
        {
            if (!inst) return;
            delete inst;
            inst = nullptr;
        }

      protected:
        /**
         * @brief 构造函数
         * 
         * 仅供派生类（如 PseudoInst 或 具体后端指令）调用。
         * @param k 指令的类别标识
         * @param c 关联的调试注释串
         */
        MInstruction(InstKind k, const std::string& c = "") : kind(k), comment(c) {}

        /// @brief 虚析构函数，确保派生类资源能够被正确回收
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
        using labelId = uint32_t;///前驱块 ID
        using srcOp   = Operand*;///源操作数
        std::map<labelId, srcOp> incomingVals;  ///< 来源映射：记录不同路径的来源值；在 PhiElimination 阶段决定插入 Move 的位置；实现 SSA 形式的合并。
        Register                 resReg;        ///< 结果寄存器：存储合并后的结果；作为后续指令的源操作数；在物理寄存器分配后被具体化。

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
        Operand* src;   ///< 源操作数：参与数据流传递；最终映射为 MOV 或 LI 指令；定义了数据的来源。
        Operand* dest;  ///< 目标操作数：接收计算结果；定义了寄存器的生命周期起点；决定了数据的去向。

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
        Register dest;        ///< 目标寄存器：用于恢复溢出到栈的值；在 StackLowering 中转换为具体的 Load 指令；作为后续计算的输入。
        int      frameIndex;  ///< 栈槽索引：关联 MFrameInfo 中的偏移量；决定了访存的具体地址计算；标识了溢出数据在栈帧中的位置。

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
        Register src;         ///< 源寄存器：待存储的物理寄存器；用于将寄存器值溢出到内存；在 StackLowering 中转换为具体的 Store 指令。
        int      frameIndex;  ///< 栈槽索引：指定溢出数据在栈帧中的位置；确保数据在函数调用或寄存器压力大时能正确保存；关联内存地址。

      public:
        FIStoreInst(Register s, int fi, const std::string& c = "")
            : PseudoInst(InstKind::SSLOT, c), src(s), frameIndex(fi)
        {}
    };

    //创建MoveInst，从源操作数src移动到目标操作数dst
    MoveInst* createMove(Operand* dst, Operand* src, const std::string& c = "");
    //创建MoveInst，从立即数imme移动到目标操作数dst
    MoveInst* createMove(Operand* dst, int imme, const std::string& c = "");
    //创建MoveInst，从立即数imme移动到目标操作数dst
    MoveInst* createMove(Operand* dst, float imme, const std::string& c = "");
}  // namespace BE

#endif  // __BACKEND_MIR_M_INSTRUCTION_H__
