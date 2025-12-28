#ifndef __BACKEND_MIR_BLOCK_H__
#define __BACKEND_MIR_BLOCK_H__

/**
 * @file m_block.h
 * @brief 后端基本块定义
 *
 * BE::Block 代表后端的一个基本块，包含一系列机器指令。
 * 与前端 IR 的 ME::Block 对应，但存储的是目标相关的 MInstruction。
 */

#include "middleend/module/ir_block.h"
#include <backend/mir/m_instruction.h>
#include <deque>

namespace BE
{
    /**
     * @brief 后端基本块
     *
     * 存储一个基本块内的所有机器指令。使用 deque 便于在头尾插入指令
     * （如 prologue/epilogue 指令）。
     */
    class Block
    {
      public:
        std::deque<MInstruction*> insts;    ///< 块内的机器指令序列
        uint32_t                  blockId;  ///< 块编号，对应 IR 的 label

      public:
        Block(uint32_t id) : blockId(id) {}
        ~Block()
        {
            for (auto inst : insts) MInstruction::delInst(inst);
            insts.clear();
        }
    };
}  // namespace BE

#endif  // __BACKEND_MIR_BLOCK_H__
