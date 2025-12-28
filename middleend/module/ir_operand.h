#ifndef __MIDDLEEND_MODULE_IR_OPERAND_H__
#define __MIDDLEEND_MODULE_IR_OPERAND_H__

#include <middleend/ir_defs.h>
#include <transfer.h>
#include <debug.h>
#include <string>
#include <sstream>
#include <map>

namespace ME
{
    class OperandFactory;

    // 操作数基类
    class Operand
    {
      private:
        OperandType type;

      protected:
        Operand(OperandType type) : type(type) {}
        virtual ~Operand() = default;

      public:
        OperandType         getType() const { return type; }
        virtual std::string toString() const    = 0;
        virtual size_t      getRegNum() const   = 0;
        virtual size_t      getLabelNum() const = 0;
    };

    // 寄存器操作数
    class RegOperand : public Operand
    {
        friend class OperandFactory;

      public:
        size_t regNum;  // 寄存器编号

      private:
        RegOperand(size_t id) : Operand(OperandType::REG), regNum(id) {}
        virtual ~RegOperand() = default;

      public:
        virtual std::string toString() const override { return "%reg_" + std::to_string(regNum); }
        virtual size_t      getRegNum() const override { return regNum; }
        virtual size_t      getLabelNum() const override { ERROR("RegOperand does not have a label number"); }
    };

    // 立即数操作数 - 整型
    class ImmeI32Operand : public Operand
    {
        friend class OperandFactory;

      public:
        int value;  // 立即数值

      private:
        ImmeI32Operand(int v) : Operand(OperandType::IMMEI32), value(v) {}
        virtual ~ImmeI32Operand() = default;

      public:
        virtual std::string toString() const override { return std::to_string(value); }
        virtual size_t      getRegNum() const override { ERROR("ImmeI32Operand does not have a register"); }
        virtual size_t      getLabelNum() const override { ERROR("ImmeI32Operand does not have a label number"); }
    };

    // 立即数操作数 - 浮点型
    class ImmeF32Operand : public Operand
    {
        friend class OperandFactory;

      public:
        float value;  // 立即数值

      private:
        ImmeF32Operand(float v) : Operand(OperandType::IMMEF32), value(v) {}
        virtual ~ImmeF32Operand() = default;

      public:
        virtual std::string toString() const override
        {
            std::stringstream ss;
            ss << "0x" << std::hex << FLOAT_TO_DOUBLE_BITS(value);
            return ss.str();
        }
        virtual size_t getRegNum() const override { ERROR("ImmeF32Operand does not have a register"); }
        virtual size_t getLabelNum() const override { ERROR("ImmeF32Operand does not have a label number"); }
    };

    // 全局变量操作数
    class GlobalOperand : public Operand
    {
        friend class OperandFactory;

      public:
        std::string name;

      private:
        GlobalOperand(const std::string n) : Operand(OperandType::GLOBAL), name(n) {}
        virtual ~GlobalOperand() = default;

      public:
        virtual std::string toString() const override { return "@" + name; }
        virtual size_t      getRegNum() const override { ERROR("GlobalOperand does not have a register"); }
        virtual size_t      getLabelNum() const override { ERROR("GlobalOperand does not have a label number"); }
    };

    // 标签操作数
    class LabelOperand : public Operand
    {
        friend class OperandFactory;

      public:
        size_t lnum;  // 标签编号

      private:
        LabelOperand(size_t num) : Operand(OperandType::LABEL), lnum(num) {}
        virtual ~LabelOperand() = default;

      public:
        virtual std::string toString() const override { return "%Block" + std::to_string(lnum); }
        virtual size_t      getRegNum() const override { ERROR("LabelOperand does not have a register"); }
        virtual size_t      getLabelNum() const override { return lnum; }
    };

    class OperandFactory
    {
      private:
        std::map<int, ImmeI32Operand*>        ImmeI32OperandMap;  // 整型立即数操作数缓存
        std::map<float, ImmeF32Operand*>      ImmeF32OperandMap;  // 浮点型立即数操作数缓存
        std::map<size_t, RegOperand*>         RegOperandMap;      // 寄存器操作数缓存
        std::map<size_t, LabelOperand*>       LabelOperandMap;    // 标签操作数缓存
        std::map<std::string, GlobalOperand*> GlobalOperandMap;   // 全局变量操作数缓存
        OperandFactory() = default;
        ~OperandFactory();

      public:
        RegOperand*     getRegOperand(size_t id);
        ImmeI32Operand* getImmeI32Operand(int value);
        ImmeF32Operand* getImmeF32Operand(float value);
        GlobalOperand*  getGlobalOperand(const std::string& name);
        LabelOperand*   getLabelOperand(size_t num);

        static OperandFactory& getInstance()
        {
            static OperandFactory instance;
            return instance;
        }
    };
}  // namespace ME

ME::RegOperand*     getRegOperand(size_t id);
ME::ImmeI32Operand* getImmeI32Operand(int value);
ME::ImmeF32Operand* getImmeF32Operand(float value);
ME::GlobalOperand*  getGlobalOperand(const std::string& name);
ME::LabelOperand*   getLabelOperand(size_t num);

std::ostream& operator<<(std::ostream& os, const ME::Operand* op);

#endif  // __MIDDLEEND_MODULE_IR_OPERAND_H__
