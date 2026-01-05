#include <backend/mir/m_defs.h>
#include <backend/mir/m_instruction.h>
#include <interfaces/middleend/ir_defs.h>
#include <debug.h>

namespace BE
{
    // Global DataType instances for common types
    //单例模式及其指针，用于表示寄存器和操作数的类型
    DataType  I32INSTANCE(DataType::Type::INT, DataType::Length::B32);
    DataType  I64INSTANCE(DataType::Type::INT, DataType::Length::B64);
    DataType  F32INSTANCE(DataType::Type::FLOAT, DataType::Length::B32);
    DataType  F64INSTANCE(DataType::Type::FLOAT, DataType::Length::B64);
    DataType  PTRINSTANCE(DataType::Type::INT, DataType::Length::B64);      // Pointer as 64-bit integer
    DataType  TOKENINSTANCE(DataType::Type::TOKEN, DataType::Length::B64);  // Token for chain dependencies
    DataType* I32   = &I32INSTANCE;
    DataType* I64   = &I64INSTANCE;
    DataType* F32   = &F32INSTANCE;
    DataType* F64   = &F64INSTANCE;
    DataType* PTR   = &PTRINSTANCE;
    DataType* TOKEN = &TOKENINSTANCE;

    //虚拟寄存器分配器，每次调用返回一个全局唯一的新虚拟寄存器
    Register getVReg(DataType* dt)
    {
        static uint32_t vreg_count = 0;
        return Register(vreg_count++, dt, true);
    }

    MoveInst* createMove(Operand* dst, Operand* src, const std::string& c) { return new MoveInst(src, dst, c); }

    MoveInst* createMove(Operand* dst, int imme, const std::string& c)
    {
        return new MoveInst(new I32Operand(imme), dst, c);
    }

    MoveInst* createMove(Operand* dst, float imme, const std::string& c)
    {
        return new MoveInst(new F32Operand(imme), dst, c);
    }

    bool Register::operator<(Register other) const
    {
        if (isVreg != other.isVreg) return isVreg < other.isVreg;
        return rId < other.rId;
    }

    bool Register::operator==(Register other) const
    {
        return rId == other.rId && isVreg == other.isVreg;
    }
}  // namespace BE
