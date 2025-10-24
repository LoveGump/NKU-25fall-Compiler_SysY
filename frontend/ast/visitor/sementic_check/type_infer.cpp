#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <limits>
#include <climits>
#include <type_traits>
#include <functional>

namespace FE::AST
{ /*
   * 本文件实现了两个typeInfer函数。读懂这段代码，你能更好地理解如何在编译期进行常量计算。
   */
    static Type* promoteType(Type* a, Type* b)
    {
        // 类型提升规则：float > long long > int
        // 获取类型A、B的基本类型
        Type_t kindA = a->getBaseType(); 
        Type_t kindB = b->getBaseType();

        if (kindA == Type_t::FLOAT || kindB == Type_t::FLOAT) return floatType;
        if (kindA == Type_t::LL || kindB == Type_t::LL) return llType;
        return intType;
    }

    static Type* getResultType(Type* operandType, Operator op)
    {
        // 根据操作符确定结果类型
        if (op == Operator::GT || op == Operator::GE || op == Operator::LT || op == Operator::LE ||
            op == Operator::EQ || op == Operator::NEQ || op == Operator::AND || op == Operator::OR ||
            op == Operator::NOT)
            return boolType; // 布尔

        if (op == Operator::ADD || op == Operator::SUB)
            if (operandType->getBaseType() == Type_t::BOOL) return intType; // 布尔参与加减运算时提升为整型

        return operandType;
    }

    template <typename T>
    static T getValue(const VarValue& val)
    {
        // 根据 VarValue 的类型获取对应的值
        Type_t valType = val.type->getBaseType();
        // constexpr if 语句用于在编译期选择合适的分支
        if constexpr (std::is_same_v<T, int>)
        {
            switch (valType)
            {
                case Type_t::BOOL: return static_cast<int>(val.boolValue);
                case Type_t::INT: return val.intValue;
                case Type_t::LL: return static_cast<int>(val.llValue);
                case Type_t::FLOAT: return static_cast<int>(val.floatValue);
                default: return 0;
            }
        }
        else if constexpr (std::is_same_v<T, long long>)
        {
            switch (valType)
            {
                case Type_t::BOOL: return static_cast<long long>(val.boolValue);
                case Type_t::INT: return static_cast<long long>(val.intValue);
                case Type_t::LL: return val.llValue;
                case Type_t::FLOAT: return static_cast<long long>(val.floatValue);
                default: return 0LL;
            }
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            switch (valType)
            {
                case Type_t::BOOL: return static_cast<float>(val.boolValue);
                case Type_t::INT: return static_cast<float>(val.intValue);
                case Type_t::LL: return static_cast<float>(val.llValue);
                case Type_t::FLOAT: return val.floatValue;
                default: return 0.0f;
            }
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            switch (valType)
            {
                case Type_t::BOOL: return val.boolValue;
                case Type_t::INT: return val.intValue != 0;
                case Type_t::LL: return val.llValue != 0;
                case Type_t::FLOAT: return val.floatValue != 0.0f;
                default: return false;
            }
        }
        else
            static_assert(sizeof(T) == 0, "Unsupported type");
    }

    template <typename T>
    static VarValue makeVarValue(T value)
    {
        // 根据传入的值类型创建对应的 VarValue
        if constexpr (std::is_same_v<T, int>)
            return VarValue(static_cast<int>(value));
        else if constexpr (std::is_same_v<T, long long>)
            return VarValue(static_cast<long long>(value));
        else if constexpr (std::is_same_v<T, float>)
            return VarValue(static_cast<float>(value));
        else if constexpr (std::is_same_v<T, bool>)
            return VarValue(static_cast<bool>(value));
        else
            static_assert(sizeof(T) == 0, "Unsupported type");
    }

    static ExprValue handleIntegerResult(long long result, Type* preferredType, bool isConst)
    {
        // 根据结果值和期望类型创建 ExprValue
        ExprValue exprVal;
        exprVal.isConstexpr = isConst;

        // 有float类型参与运算时，结果提升为float，没有就看结果值范围决定使用int还是long long
        // 对于本来就是longlong类型的操作数，结果依然保持为longlong类型
        if (preferredType->getBaseType() == Type_t::FLOAT)
            exprVal.value = VarValue(static_cast<float>(result));
        else if (result >= std::numeric_limits<int>::min() && result <= std::numeric_limits<int>::max() &&
                 preferredType->getBaseType() != Type_t::LL)
            exprVal.value = VarValue(static_cast<int>(result));
        else
            exprVal.value = VarValue(result);

        return exprVal;
    }

    template <typename T>
    static ExprValue performUnaryOp(const ExprValue& operand, Operator op, Type* resultType,
        std::vector<std::string>& errors, int lineNum, bool& hasError)
    {
        // 执行一元操作符计算
        ExprValue result;
        result.isConstexpr = operand.isConstexpr;
        result.value.type  = resultType;

        if (!result.isConstexpr) return result;

        T value = getValue<T>(operand.value);

        switch (op)
        {
            case Operator::ADD: result.value = makeVarValue(+value); break;
            case Operator::SUB:
                if constexpr (std::is_same_v<T, int>)
                {
                    if (value == INT_MIN)
                        return handleIntegerResult(2147483648LL, resultType, operand.isConstexpr);
                    else
                        return handleIntegerResult(-static_cast<long long>(value), resultType, operand.isConstexpr);
                }
                else
                    result.value = makeVarValue(-value);
                break;
            case Operator::NOT: result.value = makeVarValue(!value); break;
            default:
                errors.push_back("Invalid unary operator: " + toString(op) + " at line " + std::to_string(lineNum));
                hasError           = true;
                result.value       = VarValue();
                result.value.type  = voidType;
                result.isConstexpr = false;
                return result;
        }

        return result;
    }

    template <typename T>
    static ExprValue performBinaryOp(const ExprValue& lhs, const ExprValue& rhs, Operator op, Type* resultType,
        std::vector<std::string>& errors, int lineNum, bool& hasError)
    {
        // 执行二元操作符计算
        ExprValue result;
        result.isConstexpr = lhs.isConstexpr && rhs.isConstexpr;
        result.value.type  = resultType;

        if ((op == Operator::DIV || op == Operator::MOD) && rhs.isConstexpr)
        {
            T rvalue = getValue<T>(rhs.value);
            if (rvalue == 0)
            {
                errors.push_back(toString(op) + " operation with zero divisor at line " + std::to_string(lineNum));
                hasError           = true;
                result.value       = VarValue();
                result.value.type  = voidType;
                result.isConstexpr = false;
                return result;
            }
        }

        if (!result.isConstexpr) return result;

        T lvalue = getValue<T>(lhs.value);
        T rvalue = getValue<T>(rhs.value);

        if (op == Operator::MOD && std::is_same_v<T, float>)
        {
            errors.push_back("Invalid modulus operation for float at line " + std::to_string(lineNum));
            result.value       = VarValue();
            result.value.type  = voidType;
            result.isConstexpr = false;
            return result;
        }

        if ((op == Operator::BITOR || op == Operator::BITAND) && std::is_same_v<T, float>)
        {
            errors.push_back("Invalid bitwise operation for float at line " + std::to_string(lineNum));
            result.value       = VarValue();
            result.value.type  = voidType;
            result.isConstexpr = false;
            return result;
        }

        switch (op)
        {
            case Operator::ADD:
                if constexpr (std::is_integral_v<T>)
                    return handleIntegerResult(static_cast<long long>(lvalue) + static_cast<long long>(rvalue),
                        resultType,
                        result.isConstexpr);
                else
                    result.value = makeVarValue(lvalue + rvalue);
                break;
            case Operator::SUB:
                if constexpr (std::is_integral_v<T>)
                    return handleIntegerResult(static_cast<long long>(lvalue) - static_cast<long long>(rvalue),
                        resultType,
                        result.isConstexpr);
                else
                    result.value = makeVarValue(lvalue - rvalue);
                break;
            case Operator::MUL:
                if constexpr (std::is_integral_v<T>)
                    return handleIntegerResult(static_cast<long long>(lvalue) * static_cast<long long>(rvalue),
                        resultType,
                        result.isConstexpr);
                else
                    result.value = makeVarValue(lvalue * rvalue);
                break;
            case Operator::DIV:
                if constexpr (std::is_integral_v<T>)
                    return handleIntegerResult(static_cast<long long>(lvalue) / static_cast<long long>(rvalue),
                        resultType,
                        result.isConstexpr);
                else
                    result.value = makeVarValue(lvalue / rvalue);
                break;
            case Operator::MOD:
                if constexpr (std::is_integral_v<T>)
                    return handleIntegerResult(static_cast<long long>(lvalue) % static_cast<long long>(rvalue),
                        resultType,
                        result.isConstexpr);
                break;
            case Operator::GT: result.value = makeVarValue(lvalue > rvalue); break;
            case Operator::GE: result.value = makeVarValue(lvalue >= rvalue); break;
            case Operator::LT: result.value = makeVarValue(lvalue < rvalue); break;
            case Operator::LE: result.value = makeVarValue(lvalue <= rvalue); break;
            case Operator::EQ: result.value = makeVarValue(lvalue == rvalue); break;
            case Operator::NEQ: result.value = makeVarValue(lvalue != rvalue); break;
            case Operator::BITOR:
                if constexpr (std::is_integral_v<T>) result.value = makeVarValue(lvalue | rvalue);
                break;
            case Operator::BITAND:
                if constexpr (std::is_integral_v<T>) result.value = makeVarValue(lvalue & rvalue);
                break;
            case Operator::AND: result.value = makeVarValue(lvalue && rvalue); break;
            case Operator::OR: result.value = makeVarValue(lvalue || rvalue); break;
            case Operator::ASSIGN: result.value = makeVarValue(rvalue); break;
            default:
                errors.push_back("Invalid binary operator: " + toString(op) + " at line " + std::to_string(lineNum));
                result.value       = VarValue();
                result.value.type  = voidType;
                result.isConstexpr = false;
                return result;
        }

        return result;
    }

    static ExprValue dispatchUnaryOp(Type_t kind, const ExprValue& operand, Operator op, Type* resultType,
        std::vector<std::string>& errors, int lineNum, bool& hasError)
    {
        // 处理一元操作符
        switch (kind)
        {
            case Type_t::BOOL:
            case Type_t::INT: return performUnaryOp<int>(operand, op, resultType, errors, lineNum, hasError);
            case Type_t::LL: return performUnaryOp<long long>(operand, op, resultType, errors, lineNum, hasError);
            case Type_t::FLOAT: return performUnaryOp<float>(operand, op, resultType, errors, lineNum, hasError);
            default:
                ExprValue result;
                result.value       = VarValue();
                result.value.type  = voidType;
                result.isConstexpr = false;
                hasError           = true;
                return result;
        }
    }

    static ExprValue dispatchBinaryOp(Type_t kind, const ExprValue& lhs, const ExprValue& rhs, Operator op,
        Type* resultType, std::vector<std::string>& errors, int lineNum, bool& hasError)
    {
        // 处理二元操作符
        switch (kind)
        {
            case Type_t::BOOL:
            case Type_t::INT: return performBinaryOp<int>(lhs, rhs, op, resultType, errors, lineNum, hasError);
            case Type_t::LL: return performBinaryOp<long long>(lhs, rhs, op, resultType, errors, lineNum, hasError);
            case Type_t::FLOAT: return performBinaryOp<float>(lhs, rhs, op, resultType, errors, lineNum, hasError);
            default:
                ExprValue result;
                result.value       = VarValue();
                result.value.type  = voidType;
                result.isConstexpr = false;
                hasError           = true;
                return result;
        }
    }

    ExprValue ASTChecker::typeInfer(const ExprValue& operand, Operator op, const Node& node, bool& hasError)
    {
        // 一元操作符类型推断
        Type* operandType = operand.value.type;
        Type* resultType  = getResultType(operandType, op);

        return dispatchUnaryOp(operandType->getBaseType(), operand, op, resultType, errors, node.line_num, hasError);
    }

    ExprValue ASTChecker::typeInfer(
        const ExprValue& lhs, const ExprValue& rhs, Operator op, const Node& node, bool& hasError)
    {
        // 二元操作符类型推断
        Type* promotedType = promoteType(lhs.value.type, rhs.value.type);
        Type* resultType   = getResultType(promotedType, op);

        return dispatchBinaryOp(promotedType->getBaseType(), lhs, rhs, op, resultType, errors, node.line_num, hasError);
    }
}  // namespace FE::AST
