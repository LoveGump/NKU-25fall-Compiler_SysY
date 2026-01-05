#ifndef __BACKEND_DAG_ISD_H__
#define __BACKEND_DAG_ISD_H__

#include <debug.h>


//定义了目标无关的通用操作码
namespace BE
{
    namespace DAG
    {
        enum class ISD : unsigned
        {
            // Terminators
            RET = 1,      // 函数返回
            BR,           // 无条件跳转
            BRCOND,       // 有条件跳转

            // Token for memory/side-effect ordering
            ENTRY_TOKEN,  // DAG入口Token，用于表示副作用序列的开始
            TOKEN_FACTOR, // 合并多个Token，用于同步多个副作用路径

            // Values and constants
            COPY,//寄存器或节点间的值复制
            REG,//物理寄存器
            PHI,//SSA形式的PHI节点
            CONST_I32,//32位整型常量
            CONST_I64,//64位整型常量
            CONST_F32,//32位浮点型常量
            SYMBOL,  // global/function symbol
            LABEL,   // basic block label id

            // Memory
            LOAD,//加载操作
            STORE,//存储操作
            FRAME_INDEX,//抽象栈帧索引
            GEP,//获取元素地址

            // Integer ops
            ADD,//加法
            SUB,//减法
            MUL,//乘法
            DIV,//除法
            MOD,//取模
            SHL,//左移
            ASHR,//算术右移
            LSHR,//逻辑右移
            AND,//按位与
            OR,//按位或
            XOR,//按位异或

            // Floating ops
            FADD,//浮点数加法
            FSUB,//浮点数减法
            FMUL,//浮点数乘法
            FDIV,//浮点数除法

            // Casts/extends
            ZEXT,//零扩展
            SITOFP,//有符号整数到浮点数
            FPTOSI,//浮点数到有符号整数

            // Compares
            ICMP,//整数比较
            FCMP,//浮点数比较

            // Calls
            CALL,//调用
        };

        //生成ISD的字符串表示
        static inline const char* toString(ISD op)
        {
            switch (op)
            {
                case ISD::RET: return "RET";
                case ISD::BR: return "BR";
                case ISD::BRCOND: return "BRCOND";
                case ISD::ENTRY_TOKEN: return "ENTRY_TOKEN";
                case ISD::TOKEN_FACTOR: return "TOKEN_FACTOR";
                case ISD::COPY: return "COPY";
                case ISD::REG: return "REG";
                case ISD::PHI: return "PHI";
                case ISD::CONST_I32: return "CONST_I32";
                case ISD::CONST_I64: return "CONST_I64";
                case ISD::CONST_F32: return "CONST_F32";
                case ISD::SYMBOL: return "SYMBOL";
                case ISD::LABEL: return "LABEL";
                case ISD::LOAD: return "LOAD";
                case ISD::STORE: return "STORE";
                case ISD::FRAME_INDEX: return "FRAME_INDEX";
                case ISD::GEP: return "GEP";
                case ISD::ADD: return "ADD";
                case ISD::SUB: return "SUB";
                case ISD::MUL: return "MUL";
                case ISD::DIV: return "DIV";
                case ISD::MOD: return "MOD";
                case ISD::SHL: return "SHL";
                case ISD::ASHR: return "ASHR";
                case ISD::LSHR: return "LSHR";
                case ISD::AND: return "AND";
                case ISD::OR: return "OR";
                case ISD::XOR: return "XOR";
                case ISD::FADD: return "FADD";
                case ISD::FSUB: return "FSUB";
                case ISD::FMUL: return "FMUL";
                case ISD::FDIV: return "FDIV";
                case ISD::ZEXT: return "ZEXT";
                case ISD::SITOFP: return "SITOFP";
                case ISD::FPTOSI: return "FPTOSI";
                case ISD::ICMP: return "ICMP";
                case ISD::FCMP: return "FCMP";
                case ISD::CALL: return "CALL";
                default: ERROR("Unknown ISD opcode"); return "UNKNOWN";
            }
        }
    }  // namespace DAG
}  // namespace BE

#endif  // __BACKEND_DAG_ISD_H__
