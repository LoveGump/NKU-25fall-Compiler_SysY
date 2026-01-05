#ifndef __BACKEND_MIR_M_CODEGEN_H__
#define __BACKEND_MIR_M_CODEGEN_H__

#include <backend/mir/m_module.h>
#include <backend/mir/m_function.h>
#include <backend/mir/m_block.h>
#include <backend/mir/m_instruction.h>
#include <backend/mir/m_defs.h>
#include <iostream>

namespace BE
{
    /**
     * @brief 机器代码生成基类，负责将 MIR 转换为目标机器汇编。
     */
    class MCodeGen
    {
      public:
        /**
         * @brief 构造函数
         * @param module 指向待生成的 MIR 模块
         * @param output 汇编代码输出流
         */
        MCodeGen(Module* module, std::ostream& output)
            : module_(module), out_(output), cur_func_(nullptr), cur_block_(nullptr)
        {}

        virtual ~MCodeGen() = default;

        /**
         * @brief 执行汇编代码生成的入口函数
         */
        virtual void generateAssembly() = 0;

      protected:
        Module*       module_;    ///< 当前处理的模块
        std::ostream& out_;       ///< 汇编输出流
        Function*     cur_func_;  ///< 当前正在生成的函数
        Block*        cur_block_; ///< 当前正在生成的块

        /**
         * @brief 打印汇编文件头（如 .text, .data 段声明等）
         */
        virtual void printHeader()                        = 0;
        /**
         * @brief 打印模块中定义的所有函数
         */
        virtual void printFunctions()                     = 0;
        /**
         * @brief 打印单个函数的汇编实现
         */
        virtual void printFunction(Function* func)        = 0;
        /**
         * @brief 打印基本块及其指令
         */
        virtual void printBlock(Block* block)             = 0;
        /**
         * @brief 打印单条机器指令
         */
        virtual void printInstruction(MInstruction* inst) = 0;
        /**
         * @brief 打印全局变量定义
         */
        virtual void printGlobalDefinitions()             = 0;

        /**
         * @brief 打印寄存器操作数
         */
        virtual void printOperand(const Register& reg) = 0;
        /**
         * @brief 打印通用操作数（立即数、标签等）
         */
        virtual void printOperand(Operand* op)         = 0;

        /**
         * @brief 打印伪移动指令（通常用于寄存器分配后的处理）
         */
        virtual void printPseudoMove(MoveInst* inst);
    };
}  // namespace BE


#endif  // __BACKEND_MIR_M_CODEGEN_H__
