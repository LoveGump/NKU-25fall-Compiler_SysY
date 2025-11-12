#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>
#include <string>
#include <interfaces/ivisitor.h>

namespace FE::AST
{
    bool ASTChecker::visit(Root& node)
    {
        // TODO(Lab3-1): 实现根节点的语义检查
        // 重置符号表，遍历所有顶层语句进行检查，确保存在main函数
        // (void)node;
        // TODO("Lab3-1: Implement Root node semantic checking");

        // SysY 根节点语义检查
        // 步骤：
        // 1) 重置状态
        // 2) 单遍扫描顶层：
        //    - 遇到全局变量：立即检查并同步 glbSymbols
        //    - 遇到函数定义：先登记到 funcDecls，随后立刻检查其函数体
        // 3) 结束后检查是否存在合法的 main

        symTable.reset();
        mainExists     = false;
        funcHasReturn  = false;
        curFuncRetType = voidType;
        loopDepth      = 0;
        errors.clear();

        bool all_ok = true;

        auto* stmts = node.getStmts();
        if (!stmts)
        {
            errors.emplace_back("Missing main function");
            return false;
        }

        for (auto* s : *stmts)
        {
            if (!s) continue;  // 空语句继续分析

            if (auto* v = dynamic_cast<VarDeclStmt*>(s))
            {
                // 变量声明语句
                bool ok = apply(*this, *v);  // 立即检查变量声明
                all_ok &= ok;
                // 如果有变量声明，提取每个变量的信息同步到全局符号表快照
                if (v->decl && v->decl->decls)
                {
                    for (auto* d : *(v->decl->decls))
                    {   // 遍历每个变量声明
                        if (!d || !d->lval) continue;  // 如果左值无效则跳过
                        if (auto* l = dynamic_cast<LeftValExpr*>(d->lval))
                        {
                            if (!l->entry) continue;
                            if (auto* attr = symTable.getSymbol(l->entry)) glbSymbols[l->entry] = *attr;
                        }
                    }
                }
            }
            else if (auto* f = dynamic_cast<FuncDeclStmt*>(s))
            {
                // 函数定义语句
                if (!f->entry) continue;
                if (funcDecls.count(f->entry))
                {
                    // 重定义检查：同名函数再次出现直接报错
                    errors.emplace_back("Redefinition of function '" + f->entry->getName() + "'");
                    all_ok = false;
                    continue;
                }
                // 先登记：允许递归调用当前函数
                funcDecls[f->entry] = f;

                // main 函数签名检查（必须：int main()）
                if (f->entry->getName() == "main")
                {
                    mainExists = true;
                    size_t pc  = f->params ? f->params->size() : 0;
                    if (f->retType != intType || pc != 0)
                    {
                        // main 函数必须是int 并且没有参数
                        errors.emplace_back("Invalid signature of main (expect: int main())");
                        all_ok = false;
                    }
                }
                // 立刻检查函数体（不支持对尚未定义的函数进行调用）
                bool ok = apply(*this, *f);
                all_ok &= ok;
            }
            else
            {
                // 如果出现其他的语句直接报错
                errors.emplace_back("Top-level statement not allowed (only variable/function declarations permitted)");
                all_ok = false;
            }
        }

        if (!mainExists)
        {
            // 没有出现main函数
            errors.emplace_back("Missing main function");
            all_ok = false;
        }

        return all_ok;
    }

    void ASTChecker::libFuncRegister()
    {
        // 示例实现：注册 SysY 标准库函数到 funcDecls 中
        // 这样在语义检查时可以识别并检查对库函数的调用
        // 包括：getint, getch, getarray, getfloat, getfarray,
        //      putint, putch, putarray, putfloat, putfarray,
        //      _sysy_starttime, _sysy_stoptime
        using SymEnt = FE::Sym::Entry;

        // 注册以下库函数：
        // int getint(), getch(), getarray(int a[]);
        static SymEnt* getint   = SymEnt::getEntry("getint");
        static SymEnt* getch    = SymEnt::getEntry("getch");
        static SymEnt* getarray = SymEnt::getEntry("getarray");

        // float getfloat();
        static SymEnt* getfloat = SymEnt::getEntry("getfloat");

        // int getfarray(float a[]);
        static SymEnt* getfarray = SymEnt::getEntry("getfarray");

        // void putint(int a), putch(int a), putarray(int n, int a[]);
        static SymEnt* putint   = SymEnt::getEntry("putint");
        static SymEnt* putch    = SymEnt::getEntry("putch");
        static SymEnt* putarray = SymEnt::getEntry("putarray");

        // void putfloat(float a);
        static SymEnt* putfloat = SymEnt::getEntry("putfloat");

        // void putfarray(int n, float a[]);
        static SymEnt* putfarray = SymEnt::getEntry("putfarray");

        // void starttime(), stoptime();
        static SymEnt* _sysy_starttime = SymEnt::getEntry("_sysy_starttime");
        static SymEnt* _sysy_stoptime  = SymEnt::getEntry("_sysy_stoptime");

        // 解析参数并创建函数声明节点，加入 funcDecls 映射表
        // int getint()
        funcDecls[getint] = new FuncDeclStmt(intType, getint, nullptr);

        // int getch()
        funcDecls[getch] = new FuncDeclStmt(intType, getch, nullptr);

        // int getarray(int a[])
        auto getarray_params = new std::vector<ParamDeclarator*>();
        auto getarray_param  = new ParamDeclarator(TypeFactory::getPtrType(intType), SymEnt::getEntry("a"));
        getarray_param->attr.val.value.type = TypeFactory::getPtrType(intType);
        getarray_params->push_back(getarray_param);
        funcDecls[getarray] = new FuncDeclStmt(intType, getarray, getarray_params);

        // float getfloat()
        funcDecls[getfloat] = new FuncDeclStmt(floatType, getfloat, nullptr);

        // int getfarray(float a[])
        auto getfarray_params = new std::vector<ParamDeclarator*>();
        auto getfarray_param  = new ParamDeclarator(TypeFactory::getPtrType(floatType), SymEnt::getEntry("a"));
        getfarray_param->attr.val.value.type = TypeFactory::getPtrType(floatType);
        getfarray_params->push_back(getfarray_param);
        funcDecls[getfarray] = new FuncDeclStmt(intType, getfarray, getfarray_params);

        // void putint(int a)
        auto putint_params                = new std::vector<ParamDeclarator*>();
        auto putint_param                 = new ParamDeclarator(intType, SymEnt::getEntry("a"));
        putint_param->attr.val.value.type = intType;
        putint_params->push_back(putint_param);
        funcDecls[putint] = new FuncDeclStmt(voidType, putint, putint_params);

        // void putch(int a)
        auto putch_params                = new std::vector<ParamDeclarator*>();
        auto putch_param                 = new ParamDeclarator(intType, SymEnt::getEntry("a"));
        putch_param->attr.val.value.type = intType;
        putch_params->push_back(putch_param);
        funcDecls[putch] = new FuncDeclStmt(voidType, putch, putch_params);

        // void putarray(int n, int a[])
        auto putarray_params                 = new std::vector<ParamDeclarator*>();
        auto putarray_param1                 = new ParamDeclarator(intType, SymEnt::getEntry("n"));
        putarray_param1->attr.val.value.type = intType;
        auto putarray_param2 = new ParamDeclarator(TypeFactory::getPtrType(intType), SymEnt::getEntry("a"));
        putarray_param2->attr.val.value.type = TypeFactory::getPtrType(intType);
        putarray_params->push_back(putarray_param1);
        putarray_params->push_back(putarray_param2);
        funcDecls[putarray] = new FuncDeclStmt(voidType, putarray, putarray_params);

        // void putfloat(float a)
        auto putfloat_params                = new std::vector<ParamDeclarator*>();
        auto putfloat_param                 = new ParamDeclarator(floatType, SymEnt::getEntry("a"));
        putfloat_param->attr.val.value.type = floatType;
        putfloat_params->push_back(putfloat_param);
        funcDecls[putfloat] = new FuncDeclStmt(voidType, putfloat, putfloat_params);

        // void putfarray(int n, float a[])
        auto putfarray_params                 = new std::vector<ParamDeclarator*>();
        auto putfarray_param1                 = new ParamDeclarator(intType, SymEnt::getEntry("n"));
        putfarray_param1->attr.val.value.type = intType;
        auto putfarray_param2 = new ParamDeclarator(TypeFactory::getPtrType(floatType), SymEnt::getEntry("a"));
        putfarray_param2->attr.val.value.type = TypeFactory::getPtrType(floatType);
        putfarray_params->push_back(putfarray_param1);
        putfarray_params->push_back(putfarray_param2);
        funcDecls[putfarray] = new FuncDeclStmt(voidType, putfarray, putfarray_params);

        // void _sysy_starttime(int lineno)
        auto starttime_params                = new std::vector<ParamDeclarator*>();
        auto starttime_param                 = new ParamDeclarator(intType, SymEnt::getEntry("lineno"));
        starttime_param->attr.val.value.type = intType;
        starttime_params->push_back(starttime_param);
        funcDecls[_sysy_starttime] = new FuncDeclStmt(voidType, _sysy_starttime, starttime_params);

        // void _sysy_stoptime(int lineno)
        auto stoptime_params                = new std::vector<ParamDeclarator*>();
        auto stoptime_param                 = new ParamDeclarator(intType, SymEnt::getEntry("lineno"));
        stoptime_param->attr.val.value.type = intType;
        stoptime_params->push_back(stoptime_param);
        funcDecls[_sysy_stoptime] = new FuncDeclStmt(voidType, _sysy_stoptime, stoptime_params);
    }
}  // namespace FE::AST
