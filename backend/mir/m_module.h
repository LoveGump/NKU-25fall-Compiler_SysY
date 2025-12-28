#ifndef __BACKEND_MIR_M_MODULE_H__
#define __BACKEND_MIR_M_MODULE_H__

#include <backend/mir/m_function.h>
#include <string>
#include <vector>

namespace BE
{
    /**
     * @brief 全局变量类
     */
    class GlobalVariable
    {
      public:
        std::string      name;      ///< 变量名称
        DataType*        type;      ///< 数据类型
        std::vector<int> dims;      ///< 维度信息（为空则表示标量）

        std::vector<int> initVals;  ///< 初始值列表

        GlobalVariable(DataType* t, const std::string& n) : name(n), type(t), dims(), initVals() {}

        /**
         * @brief 判断是否为标量
         * @return true 如果是标量，否则返回 false
         */
        bool isScalar() const { return dims.empty(); }
    };

    /**
     * @brief 模块类，包含函数和全局变量的集合
     */
    class Module
    {
      public:
        std::vector<Function*>       functions; ///< 函数列表
        std::vector<GlobalVariable*> globals;   ///< 全局变量列表
    };
}  // namespace BE

#endif  // __BACKEND_MIR_M_MODULE_H__