#ifndef __MIDDLEEND_MODULE_IR_MODULE_H__
#define __MIDDLEEND_MODULE_IR_MODULE_H__

#include <middleend/module/ir_function.h>

namespace ME
{
    class Module : public Visitable
    {
      public:
        std::vector<GlbVarDeclInst*> globalVars;  // 全局变量列表
        std::vector<FuncDeclInst*>   funcDecls;   // 函数声明列表
        std::vector<Function*>       functions;   // 函数定义列表

      public:
        Module();
        ~Module();

      public:
        virtual void accept(Visitor& visitor) override { visitor.visit(*this); }
    };
}  // namespace ME

#endif  // __MIDDLEEND_MODULE_IR_MODULE_H__
