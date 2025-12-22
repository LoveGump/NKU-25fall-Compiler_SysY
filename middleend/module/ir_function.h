#ifndef __MIDDLEEND_MODULE_IR_FUNCTION_H__
#define __MIDDLEEND_MODULE_IR_FUNCTION_H__

#include <middleend/module/ir_block.h>
#include <map>

namespace ME
{
    // 函数类，包含函数定义指令和基本块列表
    class Function : public Visitable
    {
      public:
        FuncDefInst*             funcDef;  // 函数定义指令
        std::map<size_t, Block*> blocks;   // 函数基本块列表，基本块编号->基本块指针 映射

      private:
        size_t maxLabel;  // 当前函数中最大的基本块编号
        size_t maxReg;    // 当前函数中最大的寄存器编号

      public: /*以下2个变量与循环优化相关，如果你正在做Lab3，可以暂时忽略它们 */
        size_t loopStartLabel;
        size_t loopEndLabel;

      public:
        Function(FuncDefInst* fd);
        ~Function();

      public:
        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }

        Block* createBlock();
        Block* getBlock(size_t label);
        void   setMaxReg(size_t reg);
        size_t getMaxReg();
        void   setMaxLabel(size_t label);
        size_t getMaxLabel();
        size_t getNewRegId();
    };
}  // namespace ME

#endif  // __MIDDLEEND_MODULE_IR_FUNCTION_H__
