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
    enum class DataType; ///< 前端数据类型前向声明
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
        Type   dt;  ///< 类型成员变量
        Length dl;  ///< 宽度成员变量

        /// 拷贝构造函数
        DataType(const DataType& other)
        {
            this->dt = other.dt;
            this->dl = other.dl;
        }

        /// 赋值运算符重载
        DataType operator=(const DataType& other)
        {
            this->dt = other.dt;
            this->dl = other.dl;
            return *this;
        }

        /// 构造函数
        DataType(Type dt, Length dl) : dt(dt), dl(dl) {}

        /// 相等比较运算符
        bool operator==(const DataType& other) const { return this->dt == other.dt && this->dl == other.dl; }

        /// 判断两个类型是否相等
        bool equal(const DataType& other) const { return this->dt == other.dt && this->dl == other.dl; }

        /// 判断与指针指向的类型是否相等
        bool equal(const DataType* other) const
        {
            if (!other) return false;
            return this->dt == other->dt && this->dl == other->dl;
        }

        /// 获取数据类型的字节宽度
        int getDataWidth()
        {
            switch (dl)
            {
                case Length::B32: return 4;
                case Length::B64: return 8;
            }
            return 0;
        }

        /// 将类型转换为字符串描述（如 i32, f64）
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

    /// 预定义的全局数据类型实例指针
    extern DataType *I32, *I64, *F32, *F64, *PTR, *TOKEN;

    /**
     * @brief 指令种类枚举
     *
     * 定义了后端支持的伪指令类型，目标相关指令统一使用 TARGET。
     */
    enum class InstKind
    {
        NOP    = 0,   ///< 空指令，可作为注释使用
        PHI    = 1,   ///< SSA 形式的 PHI 节点
        MOVE   = 2,   ///< 寄存器间数据拷贝
        SELECT = 3,   ///< 条件选择指令
        LSLOT  = 4,   ///< 从栈槽加载数据
        SSLOT  = 5,   ///< 存储数据到栈槽
        TARGET = 100  ///< 目标架构相关的特定指令起始值
    };

    /**
     * @brief 寄存器
     *
     * 表示一个虚拟寄存器或物理寄存器。
     */
    class Register
    {
      public:
        uint32_t  rId;    ///< 寄存器唯一标识编号
        DataType* dt;     ///< 寄存器存储的数据类型
        bool      isVreg; ///< 标识是否为虚拟寄存器

      public:
        /// 构造函数
        Register(int reg = 0, DataType* dataType = nullptr, bool isV = false) : rId(reg), dt(dataType), isVreg(isV) {}

      public:
        /// 小于比较，用于 STL 容器排序
        bool operator<(Register other) const;
        /// 相等比较
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
        /// 操作数具体类型枚举
        enum class Type
        {
            REG         = 0,  ///< 寄存器操作数
            IMMI32      = 1,  ///< 32 位整数立即数
            IMMI64      = 2,  ///< 64 位整数立即数
            IMMF32      = 3,  ///< 32 位浮点立即数
            IMMF64      = 4,  ///< 64 位浮点立即数
            FRAME_INDEX = 5   ///< 栈帧索引（用于访问局部变量/溢出槽）
        };

      public:
        DataType* dt;  ///< 操作数的数据类型
        Type      ot;  ///< 操作数的种类

      public:
        /// 构造函数
        Operand(DataType* dt, Type ot) : dt(dt), ot(ot) {}
        /// 虚析构函数，确保派生类正确释放
        virtual ~Operand() = default;
    };

    /// 寄存器操作数派生类
    class RegOperand : public Operand
    {
      public:
        Register reg; ///< 关联的寄存器对象

      public:
        /// 通过寄存器构造寄存器操作数
        RegOperand(Register reg) : Operand(reg.dt, Operand::Type::REG), reg(reg) {}
    };

    /// 32位整数立即数操作数派y生类
    class I32Operand : public Operand
    {
      public:
        int val; ///< 立即数值

      public:
        /// 构造 32 位整数立即数
        I32Operand(int value) : Operand(I32, Operand::Type::IMMI32), val(value) {}
    };

    /// 32位浮点立即数操作数派生类
    class F32Operand : public Operand
    {
      public:
        float val; ///< 浮点数值

      public:
        /// 构造 32 位浮点立即数
        F32Operand(float value) : Operand(F32, Operand::Type::IMMF32), val(value) {}
    };

    /// 栈帧索引操作数派生类
    class FrameIndexOperand : public Operand
    {
      public:
        int frameIndex; ///< 栈帧中的索引位置

      public:
        /// 构造栈帧索引操作数
        FrameIndexOperand(int fi) : Operand(I64, Operand::Type::FRAME_INDEX), frameIndex(fi) {}
    };

/*
生命周期：
指令选择阶段：
  lw v1, FrameIndex(0)    // 第0个局部变量，偏移未知

帧降低阶段（Pre-RA）：
  lw v1, 16(sp)           // 计算出偏移为16

寄存器分配后：
  lw a0, 16(sp)           // 物理寄存器
*/

    /// 分配并获取一个新的虚拟寄存器
    Register getVReg(DataType* dt);
}  // namespace BE

#endif  // __BACKEND_MIR_DEFS_H__
