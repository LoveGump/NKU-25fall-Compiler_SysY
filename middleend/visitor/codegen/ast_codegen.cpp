#include <middleend/visitor/codegen/ast_codegen.h>
#include <debug.h>
#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace ME
{
    // libFuncRegister: 注册评测系统所需的库函数声明
    // 这些函数在测试时会由评测系统提供实现，编译器只需要声明它们即可
    // 包括：输入输出函数（getint, putint等）和性能测试函数（_sysy_starttime等）
    void ASTCodeGen::libFuncRegister(Module* m)
    {
        auto& decls = m->funcDecls;

        // int getint();
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getint"));

        // int getch();
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getch"));

        // int getarray(int a[]);
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getarray", {DataType::PTR}));

        // float getfloat();
        decls.emplace_back(new FuncDeclInst(DataType::F32, "getfloat"));

        // int getfarray(float a[]);
        decls.emplace_back(new FuncDeclInst(DataType::I32, "getfarray", {DataType::PTR}));

        // void putint(int a);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putint", {DataType::I32}));

        // void putch(int a);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putch", {DataType::I32}));

        // void putarray(int n, int a[]);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putarray", {DataType::I32, DataType::PTR}));

        // void putfloat(float a);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putfloat", {DataType::F32}));

        // void putfarray(int n, float a[]);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "putfarray", {DataType::I32, DataType::PTR}));

        // void starttime(int lineno);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "_sysy_starttime", {DataType::I32}));

        // void stoptime(int lineno);
        decls.emplace_back(new FuncDeclInst(DataType::VOID, "_sysy_stoptime", {DataType::I32}));

        // llvm memset
        decls.emplace_back(new FuncDeclInst(
            DataType::VOID, "llvm.memset.p0.i32", {DataType::PTR, DataType::I8, DataType::I32, DataType::I1}));
    }

    // 获取变量属性
    const FE::AST::VarAttr* ASTCodeGen::getVarAttr(FE::Sym::Entry* entry) const
    {
        // 检查符号表项是否有效
        if (!entry) return nullptr;

        // 找到对应的寄存器编号
        size_t reg = name2reg.getReg(entry);
        if (reg != static_cast<size_t>(-1))
        {
            // 如果在作用域中，直接返回变量属性
            auto locIt = reg2attr.find(reg);
            if (locIt != reg2attr.end()) return &locIt->second;
        }

        // 去全局符号表中查找
        auto glbIt = glbSymbols.find(entry);
        if (glbIt != glbSymbols.end()) return &glbIt->second;

        // 未找到返回空指针
        return nullptr;
    }

    // 处理全局变量声明
    void ASTCodeGen::handleGlobalVarDecl(FE::AST::VarDeclStmt* decls, Module* m)
    {
        // TODO(Lab 3-2): 生成全局变量声明 IR（支持标量与数组的初值）

        // 检查变量声明语句是否有效
        if (!decls || !decls->decl || !decls->decl->decls) return;

        // 遍历变量声明列表
        for (auto* vd : *(decls->decl->decls))
        {
            if (!vd) continue;
            // 获取左值表达式
            auto* lval = dynamic_cast<FE::AST::LeftValExpr*>(vd->lval);
            // 获取变量属性
            const FE::AST::VarAttr attr     = glbSymbols.at(lval->entry);
            DataType               elemType = convert(attr.type);  // 获取变量的 ir 数据类型

            if (attr.arrayDims.empty())
            {
                // 非数组变量
                Operand* initOp = nullptr;  // 初始化操作数
                if (!attr.initList.empty())
                {
                    // 如果有初始值 根据类型生成对应的立即数操作数
                    if (elemType == DataType::I32 || elemType == DataType::I1 || elemType == DataType::I8)
                        initOp = getImmeI32Operand(attr.initList[0].getInt());
                    else if (elemType == DataType::F32)
                        initOp = getImmeF32Operand(attr.initList[0].getFloat());
                }
                // 没有初始值则为零初始化

                // 添加全局变量声明指令到模块
                m->globalVars.emplace_back(new GlbVarDeclInst(elemType, lval->entry->getName(), initOp));
            }
            else
            {
                // 数组变量
                FE::AST::VarAttr arrayAttr = attr;  // 复制变量属性以修改
                size_t           totalSize = 1;     // 计算数组总大小
                // 变量初始化
                for (int dim : arrayAttr.arrayDims) { totalSize *= static_cast<size_t>(std::max(dim, 0)); }
                if (totalSize == 0) totalSize = 1;  // 防止零大小数组
                // 初始化数组元素为零值
                arrayAttr.initList.assign(totalSize, makeZeroValue(arrayAttr.type));
                if (vd->init)
                {
                    std::vector<std::pair<size_t, FE::AST::Initializer*>> initSlots;
                    gatherArrayInitializers(vd->init, arrayAttr.arrayDims, initSlots);

                    FE::AST::Type_t baseType = arrayAttr.type ? arrayAttr.type->getBaseType() : FE::AST::Type_t::INT;
                    for (const auto& slot : initSlots)
                    {
                        size_t idx      = slot.first;
                        auto*  initNode = slot.second;
                        if (!initNode || !initNode->init_val || idx >= totalSize) continue;
                        const auto& exprVal = initNode->init_val->attr.val;
                        if (!exprVal.isConstexpr) continue;

                        switch (baseType)
                        {
                            case FE::AST::Type_t::FLOAT:
                                arrayAttr.initList[idx] = FE::AST::VarValue(exprVal.getFloat());
                                break;
                            case FE::AST::Type_t::BOOL:
                                arrayAttr.initList[idx] = FE::AST::VarValue(exprVal.getBool());
                                break;
                            case FE::AST::Type_t::LL:
                                arrayAttr.initList[idx] = FE::AST::VarValue(exprVal.getLL());
                                break;
                            default: arrayAttr.initList[idx] = FE::AST::VarValue(exprVal.getInt()); break;
                        }
                    }
                }
                m->globalVars.emplace_back(new GlbVarDeclInst(elemType, lval->entry->getName(), arrayAttr));
            }
        }
    }

    void ASTCodeGen::pushLoopContext(size_t continueLabel, size_t breakLabel)
    {
        loopStack.push_back({continueLabel, breakLabel});
    }

    void ASTCodeGen::popLoopContext()
    {
        ASSERT(!loopStack.empty() && "Loop context underflow");
        loopStack.pop_back();
    }

    ASTCodeGen::LoopContext ASTCodeGen::currentLoopContext() const
    {
        ASSERT(!loopStack.empty() && "Loop context unavailable");
        return loopStack.back();
    }

    // 插入alloca指令到函数入口块
    void ASTCodeGen::insertAllocaInst(Instruction* inst)
    {
        if (!funcEntryBlock)
        {
            insert(inst);
            return;
        }

        if (curBlock == funcEntryBlock)
        {
            funcEntryBlock->insertBack(inst);
            return;
        }

        funcEntryBlock->insertFront(inst);
    }

    // 生成对应类型的零值
    FE::AST::VarValue ASTCodeGen::makeZeroValue(FE::AST::Type* type) const
    {
        if (!type) return FE::AST::VarValue(0);
        switch (type->getBaseType())
        {
            case FE::AST::Type_t::BOOL: return FE::AST::VarValue(false);
            case FE::AST::Type_t::FLOAT: return FE::AST::VarValue(0.0f);
            case FE::AST::Type_t::LL: return FE::AST::VarValue(static_cast<long long>(0));
            case FE::AST::Type_t::INT:
            default: return FE::AST::VarValue(0);
        }
    }

    // 生成数组零初始化指令
    void ASTCodeGen::emitArrayZeroInit(Operand* basePtr, DataType elemDataType, const std::vector<int>& dims)
    {
        // 计算数组总大小
        if (!basePtr || dims.empty()) return;

        size_t totalElems = 1;
        for (int dim : dims)
        {
            size_t len = static_cast<size_t>(std::max(dim, 0));
            totalElems *= len;
        }
        if (totalElems == 0) totalElems = 1;

        size_t elemSize = 0;
        switch (elemDataType)
        {
            case DataType::I1:
            case DataType::I8: elemSize = 1; break;
            case DataType::I32:
            case DataType::F32: elemSize = 4; break;
            case DataType::I64:
            case DataType::DOUBLE:
            case DataType::PTR: elemSize = 8; break;
            case DataType::VOID:
            case DataType::UNK:
            default: elemSize = 0; break;
        }
        if (elemSize == 0) return;

        size_t totalBytes = totalElems * elemSize;
        if (totalBytes == 0) return;

        ASSERT(totalBytes <= static_cast<size_t>(std::numeric_limits<int>::max()) && "memset length exceeds i32 range");

        CallInst::argList args;
        args.emplace_back(DataType::PTR, basePtr);
        args.emplace_back(DataType::I8, getImmeI32Operand(0));
        args.emplace_back(DataType::I32, getImmeI32Operand(static_cast<int>(totalBytes)));
        args.emplace_back(DataType::I1, getImmeI32Operand(0));
        insert(createCallInst(DataType::VOID, "llvm.memset.p0.i32", args));
    }

    // visit(Root): 模块级代码生成入口
    void ASTCodeGen::visit(FE::AST::Root& node, Module* m)
    {
        // 示例：注册库函数
        libFuncRegister(m);

        // TODO(Lab 3-2): 生成模块级 IR
        // 处理顶层语句：全局变量声明、函数定义等
        auto* stmts = node.getStmts();
        if (!stmts) return;  // 空语句直接返回

        for (auto* stmt : *stmts)
        {
            if (!stmt) continue;  // 空语句跳过
            if (auto* var = dynamic_cast<FE::AST::VarDeclStmt*>(stmt))
                handleGlobalVarDecl(var, m);  // 全局变量
            else if (auto* func = dynamic_cast<FE::AST::FuncDeclStmt*>(stmt))
                apply(*this, *func, m);  // 函数
        }
    }

    // 以下为指令创建辅助函数（Factory Methods）
    // 这些函数封装了指令对象的创建，简化代码生成逻辑

    // LoadInst: 从内存加载值到寄存器
    LoadInst* ASTCodeGen::createLoadInst(DataType t, Operand* ptr, size_t resReg)
    {
        return new LoadInst(t, ptr, getRegOperand(resReg));
    }

    // StoreInst: 将值存储到内存（两个重载版本）
    // ptr 为地址操作数，val 可以是寄存器操作数或立即数操作数 将val存储到ptr指向的地址
    StoreInst* ASTCodeGen::createStoreInst(DataType t, size_t valReg, Operand* ptr)
    {
        return new StoreInst(t, getRegOperand(valReg), ptr);
    }

    StoreInst* ASTCodeGen::createStoreInst(DataType t, Operand* val, Operand* ptr)
    {
        return new StoreInst(t, val, ptr);
    }

    // ArithmeticInst: 算术运算指令创建函数
    // 提供多个重载版本以支持立即数优化（减少寄存器使用）
    ArithmeticInst* ASTCodeGen::createArithmeticI32Inst(Operator op, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::I32, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticI32Inst_ImmeLeft(Operator op, int lhsVal, size_t rhsReg, size_t resReg)
    {
        // 左操作数为立即数的版本（如：%r = add i32 5, %reg）
        return new ArithmeticInst(
            op, DataType::I32, getImmeI32Operand(lhsVal), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticI32Inst_ImmeAll(Operator op, int lhsVal, int rhsVal, size_t resReg)
    {
        // 两个操作数都是立即数的版本（如：%r = add i32 5, 3）
        return new ArithmeticInst(
            op, DataType::I32, getImmeI32Operand(lhsVal), getImmeI32Operand(rhsVal), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticF32Inst(Operator op, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::F32, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticF32Inst_ImmeLeft(
        Operator op, float lhsVal, size_t rhsReg, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::F32, getImmeF32Operand(lhsVal), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    ArithmeticInst* ASTCodeGen::createArithmeticF32Inst_ImmeAll(Operator op, float lhsVal, float rhsVal, size_t resReg)
    {
        return new ArithmeticInst(
            op, DataType::F32, getImmeF32Operand(lhsVal), getImmeF32Operand(rhsVal), getRegOperand(resReg));
    }

    // IcmpInst/FcmpInst: 比较指令创建函数
    // 提供立即数右操作数版本以优化常量比较
    IcmpInst* ASTCodeGen::createIcmpInst(ICmpOp cond, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new IcmpInst(DataType::I32, cond, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    IcmpInst* ASTCodeGen::createIcmpInst_ImmeRight(ICmpOp cond, size_t lhsReg, int rhsVal, size_t resReg)
    {
        return new IcmpInst(
            DataType::I32, cond, getRegOperand(lhsReg), getImmeI32Operand(rhsVal), getRegOperand(resReg));
    }
    FcmpInst* ASTCodeGen::createFcmpInst(FCmpOp cond, size_t lhsReg, size_t rhsReg, size_t resReg)
    {
        return new FcmpInst(DataType::F32, cond, getRegOperand(lhsReg), getRegOperand(rhsReg), getRegOperand(resReg));
    }
    FcmpInst* ASTCodeGen::createFcmpInst_ImmeRight(FCmpOp cond, size_t lhsReg, float rhsVal, size_t resReg)
    {
        return new FcmpInst(
            DataType::F32, cond, getRegOperand(lhsReg), getImmeF32Operand(rhsVal), getRegOperand(resReg));
    }

    // 类型转换指令创建函数
    FP2SIInst* ASTCodeGen::createFP2SIInst(size_t srcReg, size_t destReg)
    {
        return new FP2SIInst(getRegOperand(srcReg), getRegOperand(destReg));
    }
    SI2FPInst* ASTCodeGen::createSI2FPInst(size_t srcReg, size_t destReg)
    {
        return new SI2FPInst(getRegOperand(srcReg), getRegOperand(destReg));
    }

    // ZextInst: 零扩展（主要用于 i1 -> i32）
    ZextInst* ASTCodeGen::createZextInst(size_t srcReg, size_t destReg, size_t srcBits, size_t destBits)
    {
        ASSERT(srcBits == 1 && destBits == 32 && "Currently only support i1 to i32 zext");
        return new ZextInst(DataType::I1, DataType::I32, getRegOperand(srcReg), getRegOperand(destReg));
    }

    // GEPInst: GetElementPtr 指令创建函数（用于数组索引）
    // dims: 数组维度，如 [10, 20] 表示 10x20 的二维数组
    // is: 索引操作数列表，第一个通常是 0（基地址），后续是各维索引
    // resReg: 结果寄存器编号
    GEPInst* ASTCodeGen::createGEP_I32Inst(
        DataType t, Operand* ptr, std::vector<int> dims, std::vector<Operand*> is, size_t resReg)
    {
        return new GEPInst(t, DataType::I32, ptr, getRegOperand(resReg), dims, is);
    }

    // CallInst: 函数调用指令创建函数（多个重载版本）
    // 根据是否有返回值和参数列表选择不同的构造函数
    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName, CallInst::argList args, size_t resReg)
    {
        return new CallInst(t, funcName, args, getRegOperand(resReg));
    }
    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName, CallInst::argList args)
    {
        return new CallInst(t, funcName, args);
    }
    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName, size_t resReg)
    {
        return new CallInst(t, funcName, getRegOperand(resReg));
    }
    CallInst* ASTCodeGen::createCallInst(DataType t, std::string funcName) { return new CallInst(t, funcName); }

    // RetInst: 返回指令创建函数
    // 提供多个重载以支持 void、寄存器返回值、立即数返回值
    RetInst* ASTCodeGen::createRetInst() { return new RetInst(DataType::VOID); }
    RetInst* ASTCodeGen::createRetInst(DataType t, size_t retReg) { return new RetInst(t, getRegOperand(retReg)); }
    // 以下两个函数用于"补丁"：当基本块缺少终结指令时，插入默认返回指令
    RetInst* ASTCodeGen::createRetInst(int val) { return new RetInst(DataType::I32, getImmeI32Operand(val)); }
    RetInst* ASTCodeGen::createRetInst(float val) { return new RetInst(DataType::F32, getImmeF32Operand(val)); }

    // 分支指令创建函数
    BrCondInst* ASTCodeGen::createBranchInst(size_t condReg, size_t trueTar, size_t falseTar)
    {
        return new BrCondInst(getRegOperand(condReg), getLabelOperand(trueTar), getLabelOperand(falseTar));
    }
    BrUncondInst* ASTCodeGen::createBranchInst(size_t tar) { return new BrUncondInst(getLabelOperand(tar)); }

    // AllocaInst: 栈分配指令创建函数
    // 标量版本和数组版本
    AllocaInst* ASTCodeGen::createAllocaInst(DataType t, size_t ptrReg)
    {
        return new AllocaInst(t, getRegOperand(ptrReg));
    }
    AllocaInst* ASTCodeGen::createAllocaInst(DataType t, size_t ptrReg, std::vector<int> dims)
    {
        return new AllocaInst(t, getRegOperand(ptrReg), dims);
    }

    // createTypeConvertInst: 创建类型转换指令序列
    //
    // 功能：根据源类型和目标类型，生成必要的类型转换指令
    // 返回：指令列表（可能为空，如果类型相同；可能包含多条指令，如 i1->f32 需要两步）
    //
    // 支持的转换：
    //   - i1 <-> i32: 使用 zext 或 icmp
    //   - i32 <-> f32: 使用 sitofp 或 fptosi
    //   - i1 -> f32: 先 zext 到 i32，再 sitofp 到 f32
    std::list<Instruction*> ASTCodeGen::createTypeConvertInst(DataType from, DataType to, size_t srcReg)
    {
        if (from == to) return {};  // 类型相同，无需转换
        ASSERT((from == DataType::I1) || (from == DataType::I32) || (from == DataType::F32));
        ASSERT((to == DataType::I1) || (to == DataType::I32) || (to == DataType::F32));

        std::list<Instruction*> insts;

        switch (from)
        {
            case DataType::I1:
            {
                switch (to)
                {
                    // i1 -> i32: 零扩展
                    case DataType::I32:
                    {
                        size_t    destReg = getNewRegId();
                        ZextInst* zext    = createZextInst(srcReg, destReg, 1, 32);
                        insts.push_back(zext);
                        break;
                    }
                    case DataType::F32:
                    {
                        // i1 -> f32: 先扩展到 i32，再转换为 f32
                        size_t    i32Reg = getNewRegId();
                        ZextInst* zext   = createZextInst(srcReg, i32Reg, 1, 32);
                        insts.push_back(zext);
                        size_t f32Reg = getNewRegId();
                        insts.push_back(createSI2FPInst(i32Reg, f32Reg));
                        break;
                    }
                    default: ERROR("Type conversion not supported");
                }
                break;
            }
            case DataType::I32:
            {
                switch (to)
                {
                    case DataType::I1:
                    {
                        // i32 -> i1: 与 0 比较（非零为 true）
                        size_t    destReg = getNewRegId();
                        IcmpInst* icmp    = createIcmpInst_ImmeRight(ICmpOp::NE, srcReg, 0, destReg);
                        insts.push_back(icmp);
                        break;
                    }
                    case DataType::F32:
                    {
                        // i32 -> f32: 有符号整数到浮点数转换
                        size_t destReg = getNewRegId();
                        insts.push_back(createSI2FPInst(srcReg, destReg));
                        break;
                    }
                    default: ERROR("Type conversion not supported");
                }
                break;
            }
            case DataType::F32:
            {
                switch (to)
                {
                    case DataType::I1:
                    {
                        // f32 -> i1: 与 0.0 比较（非零为 true）
                        size_t    destReg = getNewRegId();
                        FcmpInst* fcmp    = createFcmpInst_ImmeRight(FCmpOp::ONE, srcReg, 0.0f, destReg);
                        insts.push_back(fcmp);
                        break;
                    }
                    case DataType::I32:
                    {
                        // f32 -> i32: 浮点数到有符号整数转换
                        size_t destReg = getNewRegId();
                        insts.push_back(createFP2SIInst(srcReg, destReg));
                        break;
                    }
                    default: ERROR("Type conversion not supported");
                }
                break;
            }
            default: ERROR("Type conversion not supported");
        }

        return insts;
    }
}  // namespace ME
