#include <middleend/module/ir_function.h>

namespace ME
{
    Function::Function(FuncDefInst* fd)
        : funcDef(fd), blocks(), maxLabel(0), maxReg(0), loopStartLabel(0), loopEndLabel(0)
    {}
    Function::~Function()
    {
        if (funcDef)
        {
            delete funcDef;
            funcDef = nullptr;
        }
        for (auto& [label, block] : blocks)
        {
            delete block;
            block = nullptr;
        }
    }

    Block* Function::createBlock()
    {
        // 创建基本块，分配新的 label 编号
        Block* newBlock  = new Block(maxLabel);
        blocks[maxLabel] = newBlock;

        maxLabel++;
        return newBlock;
    }
    Block* Function::getBlock(size_t label)
    {
        // 根据 label 获取基本块
        if (blocks.find(label) != blocks.end()) return blocks[label];
        return nullptr;
    }
    // 设置和获取当前函数的最大寄存器编号和最大基本块编号
    void   Function::setMaxReg(size_t reg) { maxReg = reg; }
    size_t Function::getMaxReg() { return maxReg; }
    void   Function::setMaxLabel(size_t label) { maxLabel = label; }
    size_t Function::getMaxLabel() { return maxLabel; }

    // 获取一个新的寄存器编号
    size_t Function::getNewRegId() { return ++maxReg; }
}  // namespace ME
