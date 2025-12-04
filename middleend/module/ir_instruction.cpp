#include <middleend/module/ir_instruction.h>
#include <debug.h>
#include <numeric>
#include <sstream>

namespace ME
{
    // LoadInst: 从内存加载值到寄存器
    // LLVM IR 格式: %reg = load i32, ptr %ptr
    std::string LoadInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = load " << dt << ", ptr " << ptr << getComment();
        return ss.str();
    }

    // StoreInst: 将值存储到内存
    // LLVM IR 格式: store i32 %val, ptr %ptr
    std::string StoreInst::toString() const
    {
        std::stringstream ss;
         ss << "store " << dt << " " << val << ", ptr " << ptr << getComment();
        return ss.str();
    }

    // ArithmeticInst: 算术运算指令（add, sub, mul, div, mod 等）
    // LLVM IR 格式: %res = add i32 %lhs, %rhs
    // 支持的运算符由 opcode 字段决定（ADD, SUB, MUL, DIV, MOD, FADD, FSUB, FMUL, FDIV）
    std::string ArithmeticInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = " << opcode << " " << dt << " " << lhs << ", " << rhs << getComment();
        return ss.str();
    }

    // IcmpInst: 整数比较指令
    // LLVM IR 格式: %res = icmp slt i32 %lhs, %rhs
    // cond 字段指定比较类型：EQ, NE, SLT, SLE, SGT, SGE 等
    // 结果类型为 i1（布尔值）
    std::string IcmpInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = icmp " << cond << " " << dt << " " << lhs << ", " << rhs << getComment();
        return ss.str();
    }

    // FcmpInst: 浮点数比较指令
    // LLVM IR 格式: %res = fcmp olt float %lhs, %rhs
    // cond 字段指定比较类型：OEQ, ONE, OLT, OLE, OGT, OGE 等（ordered 比较）
    // 结果类型为 i1（布尔值）
    std::string FcmpInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = fcmp " << cond << " " << dt << " " << lhs << ", " << rhs << getComment();
        return ss.str();
    }

    // AllocaInst: 在栈上分配局部变量内存
    // LLVM IR 格式:
    //   - 标量: %ptr = alloca i32
    //   - 数组: %ptr = alloca [10 x i32]
    // dims 为空表示标量，否则表示多维数组的维度
    std::string AllocaInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = alloca ";
        if (dims.empty())
        {
            // 对应 %temp = alloca i32
            ss << dt << getComment();
        }
        else
        {
            // 对应 %temp = alloca [5x[10 x i32]] 即是 a[5][10] 的分配
            for (int dim : dims) ss << "[" << dim << " x ";
            ss << dt << std::string(dims.size(), ']') << getComment();
        }
        return ss.str();
    }

    // BrCondInst: 条件分支指令（终结指令）
    // LLVM IR 格式: br i1 %cond, label %true_label, label %false_label
    // 根据条件值跳转到 trueTar 或 falseTar 基本块
    std::string BrCondInst::toString() const
    {
        std::stringstream ss;
        ss << "br i1 " << cond << ", label " << trueTar << ", label " << falseTar << getComment();
        return ss.str();
    }

    // BrUncondInst: 无条件分支指令（终结指令）
    // LLVM IR 格式: br label %target
    // 无条件跳转到目标基本块
    std::string BrUncondInst::toString() const
    {
        std::stringstream ss;
        ss << "br label " << target << getComment();
        return ss.str();
    }

    /**
     * 递归初始化全局数组变量
     * @param s 输出流
     * @param type 数组元素类型
     * @param v 变量属性，包含初始化列表
     * @param dimDph 当前递归的维度深度
     * @param beginPos 初始化列表的起始位置
     * @param endPos 初始化列表的结束位置
     */
    void initArrayGlb(
        std::ostream& s, DataType type, const FE::AST::VarAttr& v, size_t dimDph, size_t beginPos, size_t endPos)
    {
        // 底层：检查是否所有元素都为0
        if (dimDph == 0)
        {  // 在递归的最外层，检查是否全为零初始化
            bool allZero = true;
            for (auto& initVal : v.initList)
            {
                // 遍历初始化列表，检查是否全为零
                if (initVal.type == FE::AST::boolType || initVal.type == FE::AST::intType ||
                    initVal.type == FE::AST::llType)
                {
                    // 可转为整数类型
                    int iv = initVal.getInt();
                    if (iv != 0) allZero = false;
                }
                if (initVal.type == FE::AST::floatType)
                {
                    // 浮点类型
                    float fv = initVal.getFloat();
                    if (fv != 0.0f) allZero = false;
                }
                if (!allZero) break;
            }

            // 如果全为0，使用 zeroinitializer 优化
            if (allZero)
            {
                // 若全为零初始化，则直接输出 zeroinitializer
                for (size_t i = 0; i < v.arrayDims.size(); ++i) s << "[" << v.arrayDims[i] << " x ";
                s << type << std::string(v.arrayDims.size(), ']') << " zeroinitializer";
                return;
            }
        }

        // 单个元素：直接输出值
        if (beginPos == endPos)
        {
            // 递归终止条件，输出单个元素的初始化值
            switch (type)
            {
                case DataType::I1:
                case DataType::I32:
                case DataType::I64: s << type << " " << v.initList[beginPos].getInt(); break;
                case DataType::F32:
                    // 浮点数以十六进制格式输出（IEEE 754 位表示）
                    s << type << " 0x" << std::hex << FLOAT_TO_DOUBLE_BITS(v.initList[beginPos].getFloat()) << std::dec;
                    break;
                default: ERROR("Unsupported data type in global array init");
            }
            return;
        }

        // 多维数组：生成类型前缀，然后递归处理每一维
        for (size_t i = dimDph; i < v.arrayDims.size(); ++i) s << "[" << v.arrayDims[i] << " x ";
        s << type << std::string(v.arrayDims.size() - dimDph, ']') << " [";

        // 计算当前维度每个元素的步长（后续维度的元素总数）
        int step = std::accumulate(v.arrayDims.begin() + dimDph + 1, v.arrayDims.end(), 1, std::multiplies<int>());
        for (int i = 0; i < v.arrayDims[dimDph]; ++i)
        {
            if (i != 0) s << ",";
            // 递归处理下一维
            initArrayGlb(s, type, v, dimDph + 1, beginPos + i * step, beginPos + (i + 1) * step - 1);
        }

        s << "]";
    }

    // GlbVarDeclInst: 全局变量声明指令
    // LLVM IR 格式:
    //   - 标量: @var = global i32 0
    //   - 数组: @arr = global [10 x i32] zeroinitializer
    //
    // 处理两种情况:
    //   1. 标量变量：使用 init 操作数或 zeroinitializer
    //   2. 数组变量：使用 initArrayGlb 递归生成初始化列表
    std::string GlbVarDeclInst::toString() const
    {
        std::stringstream ss;
        ss << "@" << name << " = global ";
        if (initList.arrayDims.empty())
        {
            // 标量变量
            ss << dt << " ";
            if (init)
                ss << init;
            else
                ss << "zeroinitializer";
        }
        else
        {
            size_t step = 1;                                 // 计算每个维度的步长
            for (int dim : initList.arrayDims) step *= dim;  // 计算总元素数量
            initArrayGlb(ss, dt, initList, 0, 0, step - 1);
        }
        ss << getComment();
        return ss.str();
    }

    // CallInst: 函数调用指令
    // LLVM IR 格式:
    //   - 有返回值: %res = call i32 @func(i32 %arg1, float %arg2)
    //   - 无返回值: call void @func(i32 %arg1)
    //
    // args 是参数列表，每个元素是 (类型, 操作数) 对
    std::string CallInst::toString() const
    {
        std::stringstream ss;
        if (retType != DataType::VOID) ss << res << " = ";
        ss << "call " << retType << " @" << funcName << "(";

        for (auto it = args.begin(); it != args.end(); ++it)
        {
            ss << it->first << " " << it->second;
            if (std::next(it) != args.end()) ss << ", ";
        }
        ss << ")" << getComment();
        return ss.str();
    }

    // RetInst: 返回指令（终结指令）
    // LLVM IR 格式:
    //   - 有返回值: ret i32 %val
    //   - 无返回值: ret void
    std::string RetInst::toString() const
    {
        std::stringstream ss;
        ss << "ret " << rt;
        if (res) ss << " " << res;
        ss << getComment();
        return ss.str();
    }

    // FuncDeclInst: 函数声明（用于外部函数或库函数）
    // LLVM IR 格式: declare i32 @func(i32, float, ...)
    //
    // 注意：这是声明而非定义，不包含函数体
    // isVarArg 表示是否支持可变参数（...）
    std::string FuncDeclInst::toString() const
    {
        std::stringstream ss;
        ss << "declare " << retType << " @" << funcName << "(";
        for (auto it = argTypes.begin(); it != argTypes.end(); ++it)
        {
            ss << *it;
            if (std::next(it) != argTypes.end()) ss << ", ";
        }
        if (isVarArg) ss << ", ...";
        ss << ")" << getComment();
        return ss.str();
    }

    // FuncDefInst: 函数定义
    // LLVM IR 格式: define i32 @func(i32 %arg1, float %arg2)
    //
    // 注意：这只是函数签名，函数体（基本块）由 Function 类管理
    // argRegs 是参数列表，每个元素是 (类型, 寄存器操作数) 对
    std::string FuncDefInst::toString() const
    {
        std::stringstream ss;
        ss << "define " << retType << " @" << funcName << "(";

        for (auto it = argRegs.begin(); it != argRegs.end(); ++it)
        {
            ss << it->first << " " << it->second;
            if (std::next(it) != argRegs.end()) ss << ", ";
        }
        ss << ")" << getComment();
        return ss.str();
    }

    std::string GEPInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = getelementptr ";
        if (dims.empty())
            ss << dt;
        else
        {
            for (int dim : dims) ss << "[" << dim << " x ";
            ss << dt << std::string(dims.size(), ']');
        }

        ss << ", ptr " << basePtr;
        for (auto& idx : idxs) ss << ", " << idxType << " " << idx;
        ss << getComment();
        return ss.str();
    }

    // SI2FPInst: 有符号整数到浮点数转换
    // LLVM IR 格式: %dest = sitofp i32 %src to float
    std::string SI2FPInst::toString() const
    {
        std::stringstream ss;
        ss << dest << " = sitofp i32 " << src << " to float" << getComment();
        return ss.str();
    }

    // FP2SIInst: 浮点数到有符号整数转换
    // LLVM IR 格式: %dest = fptosi float %src to i32
    std::string FP2SIInst::toString() const
    {
        std::stringstream ss;
        ss << dest << " = fptosi float " << src << " to i32" << getComment();
        return ss.str();
    }

    // ZextInst: 零扩展指令（通常用于 i1 -> i32 的扩展）
    // LLVM IR 格式: %dest = zext i1 %src to i32
    //
    // 将较小的整数类型零扩展到较大的整数类型
    // 常用于将布尔值（i1）转换为整数（i32）
    std::string ZextInst::toString() const
    {
        std::stringstream ss;
        ss << dest << " = zext " << from << " " << src << " to " << to << getComment();
        return ss.str();
    }

    // PhiInst: Phi 节点指令（SSA 形式中的合并点）
    // LLVM IR 格式: %res = phi i32 [ %val1, %label1 ], [ %val2, %label2 ]
    //
    // 用途:
    //   - 在基本块合并点选择来自不同前驱的值
    //   - 例如：if-else 后合并两个分支的值
    //
    // incomingVals: 映射表，key 是来源基本块标签，value 是对应的值
    std::string PhiInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = phi " << dt << " ";

        for (auto it = incomingVals.begin(); it != incomingVals.end(); ++it)
        {
            ss << "[ " << it->second << ", " << it->first << " ]";
            if (std::next(it) != incomingVals.end()) ss << ", ";
        }
        ss << getComment();
        return ss.str();
    }
}  // namespace ME
