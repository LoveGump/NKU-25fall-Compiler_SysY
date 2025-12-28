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
     * @brief 后端函数
     *
     * 存储函数的所有信息，包括：
     * - 函数名和参数列表
     * - 所有基本块（按 blockId 索引）
     * - 栈帧信息（局部变量、溢出槽等）
     */
    class Function
    {
      public:
        std::string                name;           ///< 函数名
        std::vector<Register>      params;         ///< 参数对应的虚拟寄存器列表
        std::map<uint32_t, Block*> blocks;         ///< 基本块映射：blockId -> Block*

        int                        stackSize     = 0;      ///< 栈大小（字节）
        bool                       hasStackParam = false;  ///< 是否有通过栈传递的参数
        int                        paramSize     = 0;      ///< 传出参数区大小
        std::vector<MInstruction*> allocInsts;             ///< alloca 指令列表（待处理）
        MFrameInfo                 frameInfo;              ///< 栈帧信息管理器

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
