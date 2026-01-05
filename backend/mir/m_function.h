#ifndef __BACKEND_MIR_M_FUNCTION_H__
#define __BACKEND_MIR_M_FUNCTION_H__

/**
 * @file m_function.h
 * @brief 后端函数定义
 *
 * BE::Function 代表后端的一个函数，包含函数名、参数、基本块集合以及栈帧信息。
 */

#include <backend/mir/m_block.h>
#include <backend/mir/m_frame_info.h>
#include <map>

namespace BE
{
    /**
     * @brief 后端函数 (Machine IR Function)
     *
     * BE::Function 是后端代码生成的中心数据结构，代表了一个经过指令选择后的机器级函数。
     * 它的主要作用包括：
     * 1. **控制流维护**：管理函数内部的所有基本块 (Block)，构成函数的控制流图 (CFG)。
     * 2. **栈帧管理**：通过 MFrameInfo 记录局部变量、溢出槽 (spill slots) 以及被调用者保存寄存器的布局。
     * 3. **寄存器分配**：作为寄存器分配器的基本单位，存储虚拟寄存器到物理寄存器的映射上下文。
     * 4. **代码生成**：最终汇编代码生成的直接来源，包含函数头、序言 (prologue)、主体、尾声 (epilogue) 等。
     */
    class Function
    {
      public:
        std::string                name;           ///< 函数名
        std::vector<Register>      params;         ///< 参数对应的虚拟寄存器列表
        std::map<uint32_t, Block*> blocks;         ///< 基本块映射：blockId -> Block*

        int                        stackSize     = 0;      ///< 栈大小（字节），包括局部变量和溢出空间
        bool                       hasStackParam = false;  ///< 是否有通过栈传递的输入参数
        int                        paramSize     = 0;      ///< 调用其他函数时，传出参数区的大小
        std::vector<MInstruction*> allocInsts;             ///< 待处理的 alloca 指令列表，用于计算栈空间
        MFrameInfo                 frameInfo;              ///< 栈帧详细信息管理器

      public:
        Function(const std::string& name)
            : name(name), params(), blocks(), stackSize(0), hasStackParam(false), paramSize(0), frameInfo()
        {}
        ~Function()
        {
            for (auto& [id, block] : blocks)
            {
                delete block;
                block = nullptr;
            }
            blocks.clear();
        }
    };
}  // namespace BE

#endif  // __BACKEND_MIR_M_FUNCTION_H__
