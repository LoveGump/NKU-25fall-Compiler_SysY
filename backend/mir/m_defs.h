#ifndef __BACKEND_MIR_DEFS_H__
#define __BACKEND_MIR_DEFS_H__

/**
 * @file m_defs.h
 * @brief 后端基础类型定义
 *
 * 定义了后端的核心数据结构：
 * - DataType: 数据类型（I32/I64/F32/F64/TOKEN）
 * - InstKind: 指令种类枚举
 * - Register: 寄存器（虚拟/物理）
 * - Operand: 操作数基类及其派生类
 */

#include <cstdint>
#include <string>
#include <debug.h>

namespace ME
{
    enum class DataType;
}

namespace BE
{
    /**
     * @brief 后端数据类型
     *
     * 描述操作数的类型和宽度。与前端 IR 的类型系统独立。
     */
    struct DataType
    {
        enum class Type
        {
            INT,    ///< 整数类型
            FLOAT,  ///< 浮点类型
            TOKEN   ///< 零宽度类型，用于依赖链（Chain）
        };
        enum class Length
        {
            B32,  ///< 32 位
            B64   ///< 64 位
        };
        Type   dt;  ///< 类型
        Length dl;  ///< 宽度
        DataType(const DataType& other)
        {
            this->dt = other.dt;
            this->dl = other.dl;
        }
        DataType operator=(const DataType& other)
        {
            this->dt = other.dt;
            this->dl = other.dl;
            return *this;
        }
        DataType(Type dt, Length dl) : dt(dt), dl(dl) {}
        bool operator==(const DataType& other) const { return this->dt == other.dt && this->dl == other.dl; }
        bool equal(const DataType& other) const { return this->dt == other.dt && this->dl == other.dl; }
        bool equal(const DataType* other) const
        {
            if (!other) return false;
            return this->dt == other->dt && this->dl == other->dl;
        }
        int getDataWidth()
        {
            switch (dl)
            {
                case Length::B32: return 4;
                case Length::B64: return 8;
            }
            return 0;
        }
        std::string toString()
        {
            std::string ret;
            if (dt == Type::INT) ret += 'i';
            if (dt == Type::FLOAT) ret += 'f';
            if (dl == Length::B32) ret += "32";
            if (dl == Length::B64) ret += "64";
            return ret;
        }
    };

    /// 预定义的全局数据类型实例
    extern DataType *I32, *I64, *F32, *F64, *PTR, *TOKEN;

    /**
     * @brief 指令种类枚举
     *
     * 定义了后端支持的伪指令类型，目标相关指令统一使用 TARGET。
     */
    enum class InstKind
    {
        NOP    = 0,   ///< 空指令，可作为 comment 使用
        PHI    = 1,   ///< 不同路径选择值（SSA 形式）
        MOVE   = 2,   ///< 数据拷贝
        SELECT = 3,   ///< 条件选择
        LSLOT  = 4,   ///< 内存槽加载（溢出恢复）
        SSLOT  = 5,   ///< 内存槽存储（溢出保存）
        TARGET = 100  ///< 目标相关指令
    };

    /**
     * @brief 寄存器
     *
     * 表示一个虚拟寄存器或物理寄存器。
     * - 虚拟寄存器 (isVreg=true)：由指令选择生成，在寄存器分配时分配到物理寄存器
     * - 物理寄存器 (isVreg=false)：目标架构的实际寄存器（如 x0-x31, f0-f31）
     */
    class Register
    {
      public:
        uint32_t  rId;    ///< 寄存器编号
        DataType* dt;     ///< 数据类型
        bool      isVreg; ///< 是否为虚拟寄存器

      public:
        Register(int reg = 0, DataType* dataType = nullptr, bool isV = false) : rId(reg), dt(dataType), isVreg(isV) {}

      public:
        bool operator<(Register other) const;
        bool operator==(Register other) const;
    };

    /**
     * @brief 操作数基类
     *
     * 表示指令的操作数，可以是寄存器、立即数或栈槽引用。
     */
    class Operand
    {
      public:
        enum class Type
        {
            REG         = 0,  ///< 寄存器操作数
            IMMI32      = 1,  ///< 32 位整数立即数
            IMMI64      = 2,  ///< 64 位整数立即数
            IMMF32      = 3,  ///< 32 位浮点立即数
            IMMF64      = 4,  ///< 64 位浮点立即数
            FRAME_INDEX = 5   ///< 栈槽引用（抽象的栈位置）
        };

      public:
        DataType* dt;  ///< 操作数的数据类型
        Type      ot;  ///< 操作数类型

      public:
        Operand(DataType* dt, Type ot) : dt(dt), ot(ot) {}
        virtual ~Operand() = default;
    };

    class RegOperand : public Operand
    {
      public:
        Register reg;

      public:
        RegOperand(Register reg) : Operand(reg.dt, Operand::Type::REG), reg(reg) {}
    };

    class I32Operand : public Operand
    {
      public:
        int val;

      public:
        I32Operand(int value) : Operand(I32, Operand::Type::IMMI32), val(value) {}
    };

    class F32Operand : public Operand
    {
      public:
        float val;

      public:
        F32Operand(float value) : Operand(F32, Operand::Type::IMMF32), val(value) {}
    };

    class FrameIndexOperand : public Operand
    {
      public:
        int frameIndex;

      public:
        FrameIndexOperand(int fi) : Operand(I64, Operand::Type::FRAME_INDEX), frameIndex(fi) {}
    };

    Register getVReg(DataType* dt);
}  // namespace BE

#endif  // __BACKEND_MIR_DEFS_H__
