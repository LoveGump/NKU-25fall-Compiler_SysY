#include <middleend/module/ir_instruction.h>
#include <debug.h>
#include <numeric>
#include <sstream>



namespace ME
{
    std::string LoadInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = load " << dt << ", " << dt << "* " << ptr << getComment();
        return ss.str();
    }

    std::string StoreInst::toString() const
    {
        std::stringstream ss;
        ss << "store " << dt << " " << val << ", " << dt << "* " << ptr << getComment();
        return ss.str();
    }

    std::string ArithmeticInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = " << opcode << " " << dt << " " << lhs << ", " << rhs << getComment();
        return ss.str();
    }

    std::string IcmpInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = icmp " << cond << " " << dt << " " << lhs << ", " << rhs << getComment();
        return ss.str();
    }

    std::string FcmpInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = fcmp " << cond << " " << dt << " " << lhs << ", " << rhs << getComment();
        return ss.str();
    }

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

    std::string BrCondInst::toString() const
    {
        std::stringstream ss;
        ss << "br i1 " << cond << ", label " << trueTar << ", label " << falseTar << getComment();
        return ss.str();
    }

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
    void initArrayGlb( std::ostream& s, DataType type, const FE::AST::VarAttr& v, size_t dimDph, size_t beginPos, size_t endPos)
    {
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

            if (allZero)
            {
                // 若全为零初始化，则直接输出 zeroinitializer
                for (size_t i = 0; i < v.arrayDims.size(); ++i) s << "[" << v.arrayDims[i] << " x ";
                s << type << std::string(v.arrayDims.size(), ']') << " zeroinitializer";
                return;
            }
        }

        if (beginPos == endPos)
        {
            // 递归终止条件，输出单个元素的初始化值
            switch (type)
            {
                case DataType::I1:
                case DataType::I32:
                case DataType::I64: s << type << " " << v.initList[beginPos].getInt(); break;
                case DataType::F32:
                    s << type << " 0x" << std::hex << FLOAT_TO_DOUBLE_BITS(v.initList[beginPos].getFloat()) << std::dec;
                    break;
                default: ERROR("Unsupported data type in global array init");
            }
            return;
        }

        for (size_t i = dimDph; i < v.arrayDims.size(); ++i) s << "[" << v.arrayDims[i] << " x ";
        s << type << std::string(v.arrayDims.size() - dimDph, ']') << " [";

        int step = std::accumulate(v.arrayDims.begin() + dimDph + 1, v.arrayDims.end(), 1, std::multiplies<int>());
        for (int i = 0; i < v.arrayDims[dimDph]; ++i)
        {
            if (i != 0) s << ",";
            initArrayGlb(s, type, v, dimDph + 1, beginPos + i * step, beginPos + (i + 1) * step - 1);
        }

        s << "]";
    }
    
    std::string GlbVarDeclInst::toString() const
    {
        std::stringstream ss;
        ss << "@" << name << " = global ";
        if (initList.arrayDims.empty())
        {
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

    std::string CallInst::toString() const
    {
        std::stringstream ss;
        if (retType != DataType::VOID) ss << res << " = ";
        ss << "call " << retType << " @" << funcName << "(";

        for (size_t idx = 0; idx < args.size(); ++idx)
        {
            // 参数类型优先打印 argTypeStrs，ptr需要指明类型
            if (!argTypeStrs.empty() && idx < argTypeStrs.size() && !argTypeStrs[idx].empty())
                ss << argTypeStrs[idx];
            else
                ss << args[idx].first;
            ss << " " << args[idx].second;

            // 如果不是最后一个参数，添加逗号
            if (idx + 1 < args.size()) ss << ", ";
        }
        ss << ")" << getComment();
        return ss.str();
    }

    std::string RetInst::toString() const
    {
        std::stringstream ss;
        ss << "ret " << rt;
        if (res) ss << " " << res;
        ss << getComment();
        return ss.str();
    }

    std::string FuncDeclInst::toString() const
    {
        std::stringstream ss;
        ss << "declare " << retType << " @" << funcName << "(";
        for (size_t idx = 0; idx < argTypes.size(); ++idx)
        {
            // 优先 argTypeStrs
            if (!argTypeStrs.empty() && idx < argTypeStrs.size() && !argTypeStrs[idx].empty()){
                ss << argTypeStrs[idx];
            }
            else{
                ss << argTypes[idx];
            }
            if (idx + 1 < argTypes.size()) ss << ", ";
        }
        if (isVarArg) ss << ", ...";
        ss << ")" << getComment();
        return ss.str();
    }

    std::string FuncDefInst::toString() const
    {
        std::stringstream ss;
        ss << "define " << retType << " @" << funcName << "(";

        for (size_t idx = 0; idx < argRegs.size(); ++idx)
        {
            // 优先 argTypeStrs
            if (!argTypeStrs.empty() && idx < argTypeStrs.size() && !argTypeStrs[idx].empty())
                ss << argTypeStrs[idx];
            else
                ss << argRegs[idx].first;
            ss << " " << argRegs[idx].second;
            if (idx + 1 < argRegs.size()) ss << ", ";
        }
        ss << ")" << getComment();
        return ss.str();
    }

    // 辅助函数 生成聚合类型字符串表示，主要是来表示数组类型
    std::string aggregateTypeString(ME::DataType elemType, const std::vector<int>& dims)
    {
        std::stringstream ss;
        if (dims.empty()) { 
            // 非数组类型
            ss << elemType; 
        }
        else
        {
            // 数组类型
            for (int dim : dims) ss << "[" << dim << " x ";
            ss << elemType << std::string(dims.size(), ']');
        }
        return ss.str();
    }
    std::string GEPInst::toString() const
    {
        std::stringstream ss;
        ss << res << " = getelementptr ";
        std::string aggType = aggregateTypeString(dt, dims);
        ss << aggType;

        ss << ", " << aggType << "* " << basePtr;
        for (auto& idx : idxs) ss << ", " << idxType << " " << idx;
        ss << getComment();
        return ss.str();
    }

    std::string SI2FPInst::toString() const
    {
        std::stringstream ss;
        ss << dest << " = sitofp i32 " << src << " to float" << getComment();
        return ss.str();
    }

    std::string FP2SIInst::toString() const
    {
        std::stringstream ss;
        ss << dest << " = fptosi float " << src << " to i32" << getComment();
        return ss.str();
    }

    std::string ZextInst::toString() const
    {
        std::stringstream ss;
        ss << dest << " = zext " << from << " " << src << " to " << to << getComment();
        return ss.str();
    }

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
