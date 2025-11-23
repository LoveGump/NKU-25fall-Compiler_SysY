#ifndef __MIDDLEEND_VISITOR_CODEGEN_AST_CODEGEN_H__
#define __MIDDLEEND_VISITOR_CODEGEN_AST_CODEGEN_H__

#include <frontend/ast/ast_visitor.h>
#include <frontend/ast/ast.h>
#include <frontend/ast/decl.h>
#include <frontend/ast/expr.h>
#include <frontend/ast/stmt.h>
#include <middleend/ir_defs.h>
#include <middleend/module/ir_module.h>
#include <debug.h>
#include <list>

/*
 * Lab 3-2: 中间代码生成 (IR Generation)
 *
 * 本文件定义了 `ME::ASTCodeGen`，用于遍历 AST 并生成 IR。
 *
 * 已保留的示例实现（可作为参考，不要求修改）:
 * - `libFuncRegister`：注册评测用/内建库函数的 IR 声明
 * - `visit(FE::AST::LiteralExpr&)`：字面量到常量指令的基本转换
 * - `visit(FE::AST::ExprStmt&)`：顺序访问表达式语句的最小示例
 *
 * 剩余的 visit 与辅助方法需你给出具体实现
 *
 * 生成 IR 的基本约束与提示:
 * - 基本块必须以终结指令结尾（ret/br）。如无法静态判定，需补充安全的“收尾”ret。
 *   即，你可能需要在所有基本块生成结束后，手动补充 ret 指令。
 * - 按需进行类型转换（i1 <-> i32、i32 <-> f32），保持算术/比较操作数类型一致。
 * - 变量：局部使用 alloca+store 初始化；数组索引使用 GEP（getelementptr) 取址；全局需在 Module 层声明。
 * - 控制流：为 if/while/for 构造清晰的 cond/body/step/end 等基本块，并正确连边。
 * - 调用：准备参数寄存器，必要时插入转换；根据返回类型决定是否分配返回寄存器。
 */

namespace ME
{
    using CodeGen_t = FE::AST::Visitor_t<void, Module*>;

    struct UnaryOperators;
    struct BinaryOperators;

    class ASTCodeGen : public CodeGen_t
    {
        friend struct UnaryOperators;
        friend struct BinaryOperators;

      private:
        // 全局符号表：存储全局变量的符号表项到变量属性的映射
        const std::map<FE::Sym::Entry*, FE::AST::VarAttr>&       glbSymbols;
        // 函数声明表：存储函数符号表项到函数声明AST节点的映射
        const std::map<FE::Sym::Entry*, FE::AST::FuncDeclStmt*>& funcDecls;
        // 当前正在生成的函数（IR Function对象）
        Function*                                                curFunc;
        // 当前正在生成的基本块（IR Block对象）
        Block*                                                   curBlock;

        // 符号到寄存器的映射表（支持作用域嵌套）
        // 用于查找变量对应的寄存器编号
        class RegTab
        {
          public:
          // 作用域结构：每个作用域维护一个符号到寄存器的映射
            struct Scope
            {
                std::map<FE::Sym::Entry*, size_t> sym2Reg;// 符号 -> 寄存器编号
                Scope*                            parent;// 父作用域（用于作用域链查找）

                Scope(Scope* parent = nullptr) : sym2Reg(), parent(parent) {}
                ~Scope() = default;
            };
            Scope* curScope;// 当前作用域

          public:
            RegTab() : curScope(new Scope(nullptr)) {}
            // ... 析构函数和成员函数 ...
            ~RegTab()
            {
                Scope* scope = curScope;
                while (scope)
                {
                    Scope* parent = scope->parent;
                    delete scope;
                    scope = parent;
                }
            }

          public:
            // 在当前作用域添加符号到寄存器的映射
            void   addSymbol(FE::Sym::Entry* entry, size_t reg) { curScope->sym2Reg[entry] = reg; }
            // 从当前作用域开始向上查找符号对应的寄存器（支持作用域链）
            size_t getReg(FE::Sym::Entry* entry)
            {
                Scope* scope = curScope;
                while (scope)
                {
                    if (scope->sym2Reg.find(entry) != scope->sym2Reg.end()) return scope->sym2Reg[entry];
                    scope = scope->parent;
                }
                return static_cast<size_t>(-1);
            }
            // 进入新的作用域（创建子作用域）
            void enterScope() { curScope = new Scope(curScope); }
            // 退出当前作用域（回到父作用域）
            void exitScope()
            {
                ASSERT(curScope != nullptr && "No scope to exit");
                Scope* parent = curScope->parent;
                delete curScope;
                curScope = parent;
            }
        } name2reg;
        // 寄存器到变量属性的映射（用于获取变量的类型、维度等信息）
        std::map<size_t, FE::AST::VarAttr>        reg2attr;
        // 参数指针表：标记第i个参数是否为指针类型（用于数组参数处理）
        std::map<size_t, bool>                    paramPtrTab;
        // 左值表达式到指针操作数的映射（用于赋值时获取左值的地址）
        std::map<FE::AST::LeftValExpr*, Operand*> lval2ptr;

      public:
        ASTCodeGen(const std::map<FE::Sym::Entry*, FE::AST::VarAttr>& glbSymbols,
            const std::map<FE::Sym::Entry*, FE::AST::FuncDeclStmt*>&  funcDecls)
            : glbSymbols(glbSymbols),
              funcDecls(funcDecls),
              curFunc(nullptr),
              curBlock(nullptr),
              name2reg(),
              reg2attr(),
              paramPtrTab(),
              lval2ptr()
        {}

      private:
        // Basic AST nodes
        void visit(FE::AST::Root& node, Module* m) override;

        // Declaration nodes
        void visit(FE::AST::Initializer& node, Module* m) override;
        void visit(FE::AST::InitializerList& node, Module* m) override;
        void visit(FE::AST::VarDeclarator& node, Module* m) override;
        void visit(FE::AST::ParamDeclarator& node, Module* m) override;
        void visit(FE::AST::VarDeclaration& node, Module* m) override;

        // Expression nodes
        void visit(FE::AST::LeftValExpr& node, Module* m) override;
        void visit(FE::AST::LiteralExpr& node, Module* m) override;
        void visit(FE::AST::UnaryExpr& node, Module* m) override;
        void visit(FE::AST::BinaryExpr& node, Module* m) override;
        void visit(FE::AST::CallExpr& node, Module* m) override;
        void visit(FE::AST::CommaExpr& node, Module* m) override;

        // Statement nodes
        void visit(FE::AST::ExprStmt& node, Module* m) override;
        void visit(FE::AST::FuncDeclStmt& node, Module* m) override;
        void visit(FE::AST::VarDeclStmt& node, Module* m) override;
        void visit(FE::AST::BlockStmt& node, Module* m) override;
        void visit(FE::AST::ReturnStmt& node, Module* m) override;
        void visit(FE::AST::WhileStmt& node, Module* m) override;
        void visit(FE::AST::IfStmt& node, Module* m) override;
        void visit(FE::AST::BreakStmt& node, Module* m) override;
        void visit(FE::AST::ContinueStmt& node, Module* m) override;
        void visit(FE::AST::ForStmt& node, Module* m) override;

        void handleAssign(FE::AST::LeftValExpr& lhs, FE::AST::ExprNode& rhs, Module* m);
        void handleLogicalAnd(FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m);
        void handleLogicalOr(FE::AST::BinaryExpr& node, FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, Module* m);

      private:
        void libFuncRegister(Module* m);
        void handleGlobalVarDecl(FE::AST::VarDeclStmt* decls, Module* m);

      private:
        void   enterFunc(Function* func) { curFunc = func; }
        void   exitFunc() { curFunc = nullptr; }
        void   enterBlock(Block* block) { curBlock = block; }
        void   enterBlock(size_t label) { curBlock = curFunc->getBlock(label); }
        void   exitBlock() { curBlock = nullptr; }
        Block* createBlock() { return curFunc->createBlock(); }
        Block* getBlock(size_t label) { return curFunc->getBlock(label); }
        size_t getMaxReg() { return curFunc->getMaxReg(); }
        size_t getMaxLabel() { return curFunc->getMaxLabel(); }
        size_t getNewRegId() { return curFunc->getNewRegId(); }
        void   insert(Instruction* inst) { curBlock->insertBack(inst); }

      private:
        DataType convert(FE::AST::Type* at);
        void     handleUnaryCalc(FE::AST::ExprNode& node, FE::AST::Operator uop, Block* block, Module* m);
        void     handleBinaryCalc(
                FE::AST::ExprNode& lhs, FE::AST::ExprNode& rhs, FE::AST::Operator bop, Block* block, Module* m);

      private:
        LoadInst* createLoadInst(DataType t, Operand* ptr, size_t resReg);

        StoreInst* createStoreInst(DataType t, size_t valReg, Operand* ptr);
        StoreInst* createStoreInst(DataType t, Operand* val, Operand* ptr);

        ArithmeticInst* createArithmeticI32Inst(Operator op, size_t lhsReg, size_t rhsReg, size_t resReg);
        ArithmeticInst* createArithmeticI32Inst_ImmeLeft(Operator op, int lhsVal, size_t rhsReg, size_t resReg);
        ArithmeticInst* createArithmeticI32Inst_ImmeAll(Operator op, int lhsVal, int rhsVal, size_t resReg);
        ArithmeticInst* createArithmeticF32Inst(Operator op, size_t lhsReg, size_t rhsReg, size_t resReg);
        ArithmeticInst* createArithmeticF32Inst_ImmeLeft(Operator op, float lhsVal, size_t rhsReg, size_t resReg);
        ArithmeticInst* createArithmeticF32Inst_ImmeAll(Operator op, float lhsVal, float rhsVal, size_t resReg);

        IcmpInst* createIcmpInst(ICmpOp cond, size_t lhsReg, size_t rhsReg, size_t resReg);
        IcmpInst* createIcmpInst_ImmeRight(ICmpOp cond, size_t lhsReg, int rhsVal, size_t resReg);
        FcmpInst* createFcmpInst(FCmpOp cond, size_t lhsReg, size_t rhsReg, size_t resReg);
        FcmpInst* createFcmpInst_ImmeRight(FCmpOp cond, size_t lhsReg, float rhsVal, size_t resReg);

        FP2SIInst* createFP2SIInst(size_t srcReg, size_t destReg);
        SI2FPInst* createSI2FPInst(size_t srcReg, size_t destReg);
        ZextInst*  createZextInst(size_t srcReg, size_t destReg, size_t srcBits, size_t destBits);

        GEPInst* createGEP_I32Inst(
            DataType t, Operand* ptr, std::vector<int> dims, std::vector<Operand*> is, size_t resReg);

        CallInst* createCallInst(DataType t, std::string funcName, CallInst::argList args, size_t resReg);
        CallInst* createCallInst(DataType t, std::string funcName, CallInst::argList args);
        CallInst* createCallInst(DataType t, std::string funcName, size_t resReg);
        CallInst* createCallInst(DataType t, std::string funcName);

        RetInst* createRetInst();
        RetInst* createRetInst(DataType t, size_t retReg);
        // 下面两个函数仅用于打补丁，如对于下面这个函数：
        /*
            int foo()
            {
                if (1) return 0;
                else return 1;
            }
            在从 AST 翻译到 IR 的过程中，if 语句本身并不知道 then 分支后面没有任何内容了
            因此会 then 对应的基本块会是空的，而 llvm ir 语义要求每个基本块都必须以 terminator instruction 结尾
            因此手动插入一个 ret 指令作为补丁。如果后续做了死代码消除，这些补丁指令会被清理掉
        */
        RetInst* createRetInst(int val);
        RetInst* createRetInst(float val);

        BrCondInst*   createBranchInst(size_t condReg, size_t trueTar, size_t falseTar);
        BrUncondInst* createBranchInst(size_t tar);

        AllocaInst* createAllocaInst(DataType t, size_t ptrReg);
        AllocaInst* createAllocaInst(DataType t, size_t ptrReg, std::vector<int> dims);

        std::list<Instruction*> createTypeConvertInst(DataType from, DataType to, size_t srcReg);
    };
}  // namespace ME

#endif  // __MIDDLEEND_VISITOR_CODEGEN_AST_CODEGEN_H__
