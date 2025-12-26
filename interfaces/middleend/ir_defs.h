#ifndef __INTERFACES_MIDDLEEND_IR_DEFS_H__
#define __INTERFACES_MIDDLEEND_IR_DEFS_H__

#include <iostream>
#include <map>

#define IR_DATATYPE  \
    X(UNK, unk, 0)   \
    X(I32, i32, 1)   \
    X(F32, float, 2) \
    X(PTR, ptr, 3)   \
    X(VOID, void, 4) \
    X(I8, i8, 5)     \
    X(I1, i1, 6)     \
    X(I64, i64, 7)   \
    X(DOUBLE, double, 8)

#define IR_OPERAND_TYPE \
    X(UNKNOWN, 0)       \
    X(REG, 1)           \
    X(IMMEI32, 2)       \
    X(IMMEF32, 3)       \
    X(GLOBAL, 4)        \
    X(LABEL, 5)         \
    X(IMMEI64, 6)

#define IR_OPCODE                       \
    X(OTHER, other, 0)                  \
    X(LOAD, load, 1)                    \
    X(STORE, store, 2)                  \
    X(ADD, add, 3)                      \
    X(SUB, sub, 4)                      \
    X(ICMP, icmp, 5)                    \
    X(PHI, phi, 6)                      \
    X(ALLOCA, alloca, 7)                \
    X(MUL, mul, 8)                      \
    X(DIV, sdiv, 9)                     \
    X(BR_COND, br, 10)                  \
    X(BR_UNCOND, br, 11)                \
    X(FADD, fadd, 12)                   \
    X(FSUB, fsub, 13)                   \
    X(FMUL, fmul, 14)                   \
    X(FDIV, fdiv, 15)                   \
    X(FCMP, fcmp, 16)                   \
    X(MOD, srem, 17)                    \
    X(BITXOR, xor, 18)                  \
    X(BITAND, and, 19)                  \
    X(RET, ret, 20)                     \
    X(ZEXT, zext, 21)                   \
    X(SHL, shl, 22)                     \
    X(ASHR, ashr, 23)                   \
    X(LSHR, lshr, 24)                   \
    X(FPTOSI, fptosi, 25)               \
    X(GETELEMENTPTR, getelementptr, 26) \
    X(CALL, call, 27)                   \
    X(SITOFP, sitofp, 28)               \
    X(GLOBAL_VAR, global_var, 29)       \
    X(FPEXT, fpext, 30)                 \
    X(EMPTY, empty, 31)                 \
    X(FUNCDECL, func_decl, 32)          \
    X(FUNCDEF, func_def, 33)

#define IR_ICMP    \
    X(EQ, eq, 1)   \
    X(NE, ne, 2)   \
    X(UGT, ugt, 3) \
    X(UGE, uge, 4) \
    X(ULT, ult, 5) \
    X(ULE, ule, 6) \
    X(SGT, sgt, 7) \
    X(SGE, sge, 8) \
    X(SLT, slt, 9) \
    X(SLE, sle, 10)

#define IR_FCMP     \
    X(OEQ, oeq, 1)  \
    X(OGT, ogt, 2)  \
    X(OGE, oge, 3)  \
    X(OLT, olt, 4)  \
    X(OLE, ole, 5)  \
    X(ONE, one, 6)  \
    X(ORD, ord, 7)  \
    X(UEQ, ueq, 8)  \
    X(UGT, ugt, 9)  \
    X(UGE, uge, 10) \
    X(ULT, ult, 11) \
    X(ULE, ule, 12) \
    X(UNE, une, 13) \
    X(UNO, uno, 14)

namespace ME
{
    enum class DataType
    {
#define X(name, str, val) name = val,
        IR_DATATYPE
#undef X
    };

    enum class OperandType
    {
#define X(name, val) name = val,
        IR_OPERAND_TYPE
#undef X
    };

    enum class Operator
    {
#define X(name, str, val) name = val,
        IR_OPCODE
#undef X
    };

    enum class ICmpOp
    {
#define X(name, str, val) name = val,
        IR_ICMP
#undef X
    };

    enum class FCmpOp
    {
#define X(name, str, val) name = val,
        IR_FCMP
#undef X
    };

    using RegMap   = std::map<size_t, size_t>;  // 寄存器号 -> 重命名后寄存器号
    using LabelMap = std::map<size_t, size_t>;  // 标签号 -> 重命名后标签号
}  // namespace ME

std::ostream& operator<<(std::ostream& os, ME::DataType dt);
std::ostream& operator<<(std::ostream& os, ME::Operator op);
std::ostream& operator<<(std::ostream& os, ME::ICmpOp cop);
std::ostream& operator<<(std::ostream& os, ME::FCmpOp cop);

#endif  // __INTERFACES_MIDDLEEND_IR_DEFS_H__
