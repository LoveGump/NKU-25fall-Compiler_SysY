#include <middleend/module/ir_operand.h>

namespace ME
{
    OperandFactory::~OperandFactory()
    {
        for (auto& [k, v] : ImmeI32OperandMap) delete v;
        for (auto& [k, v] : ImmeF32OperandMap) delete v;
        for (auto& [k, v] : RegOperandMap) delete v;
        for (auto& [k, v] : LabelOperandMap) delete v;
        for (auto& [k, v] : GlobalOperandMap) delete v;
    }

    // 内部查找缓存，若不存在则 new 一个对象并缓存后返回同一指针

    RegOperand* OperandFactory::getRegOperand(size_t id)
    {
        auto it = RegOperandMap.find(id);
        if (it == RegOperandMap.end())
        {
            RegOperand* op    = new RegOperand(id);
            RegOperandMap[id] = op;
            return op;
        }
        return it->second;
    }

    ImmeI32Operand* OperandFactory::getImmeI32Operand(int value)
    {
        auto it = ImmeI32OperandMap.find(value);
        if (it == ImmeI32OperandMap.end())
        {
            ImmeI32Operand* op       = new ImmeI32Operand(value);
            ImmeI32OperandMap[value] = op;
            return op;
        }
        return it->second;
    }

    ImmeF32Operand* OperandFactory::getImmeF32Operand(float value)
    {
        auto it = ImmeF32OperandMap.find(value);
        if (it == ImmeF32OperandMap.end())
        {
            ImmeF32Operand* op       = new ImmeF32Operand(value);
            ImmeF32OperandMap[value] = op;
            return op;
        }
        return it->second;
    }

    GlobalOperand* OperandFactory::getGlobalOperand(const std::string& name)
    {
        auto it = GlobalOperandMap.find(name);
        if (it == GlobalOperandMap.end())
        {
            GlobalOperand* op      = new GlobalOperand(name);
            GlobalOperandMap[name] = op;
            return op;
        }
        return it->second;
    }

    LabelOperand* OperandFactory::getLabelOperand(size_t num)
    {
        auto it = LabelOperandMap.find(num);
        if (it == LabelOperandMap.end())
        {
            LabelOperand* op     = new LabelOperand(num);
            LabelOperandMap[num] = op;
            return op;
        }
        return it->second;
    }

    OperandFactory& ofInstance = OperandFactory::getInstance();
}  // namespace ME

// 全局访问接口，方便外部获取操作数对象
ME::RegOperand*     getRegOperand(size_t id) { return ME::ofInstance.getRegOperand(id); }
ME::ImmeI32Operand* getImmeI32Operand(int value) { return ME::ofInstance.getImmeI32Operand(value); }
ME::ImmeF32Operand* getImmeF32Operand(float value) { return ME::ofInstance.getImmeF32Operand(value); }
ME::GlobalOperand*  getGlobalOperand(const std::string& name) { return ME::ofInstance.getGlobalOperand(name); }
ME::LabelOperand*   getLabelOperand(size_t num) { return ME::ofInstance.getLabelOperand(num); }

std::ostream& operator<<(std::ostream& os, const ME::Operand* op)
{
    // 重载 << 运算符，输出指针的时候，其实是调用 toString 方法
    os << op->toString();
    return os;
}
