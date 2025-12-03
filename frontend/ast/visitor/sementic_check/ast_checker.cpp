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
        this->symTable.reset();
        this->mainExists     = false;
        this->funcHasReturn  = false;
        this->curFuncRetType = voidType;
        this->loopDepth      = 0;
        this->errors.clear();

        bool success = true;

        auto* stmts = node.getStmts();
        if (!stmts)
        {
            // 空程序，直接检查main函数
            this->errors.push_back("Error: 'main' function is missing.");
            return false;
        }

        for (auto* stmt : *stmts)
        {
            if (!stmt) continue;  // 空语句，跳过
            if (auto* varDecl = dynamic_cast<VarDeclStmt*>(stmt))
            {
                // 对于全局变量声明语句，在这里判断是否重复定义
                success = apply(*this, *varDecl) && success;
                // 如果有变量声明，提取其信息到全局符号表
                if (varDecl->decl && this->symTable.isGlobalScope())
                {
                    for (auto* decl : *(varDecl->decl->decls))
                    {
                        // 遍历每个变量声明器
                        if (!decl || !decl->lval) continue;
                        if (auto* Lval = dynamic_cast<LeftValExpr*>(decl->lval))
                        {
                            // 如果左值表达式有效，提取符号表项和属性
                            if (!Lval->entry) continue;
                            if (auto* attr = this->symTable.getSymbol(Lval->entry))
                            {
                                // 将符号及其属性加入全局符号表
                                this->glbSymbols[Lval->entry] = *attr;
                            }
                        }
                    }
                }
            }
            else if (auto* funcDecl = dynamic_cast<FuncDeclStmt*>(stmt))
            {
                // 函数定义语句
                if (!funcDecl->entry) continue;

                // 重定义检查
                if (this->funcDecls.find(funcDecl->entry) != this->funcDecls.end())
                {
                    this->errors.push_back("Error: Redefinition of function '" + funcDecl->entry->getName() +
                                           "' at line " + std::to_string(funcDecl->line_num) + ".");
                    success = false;
                    continue;
                }
                // 记录函数声明
                this->funcDecls[funcDecl->entry] = funcDecl;

                // 检查是否为 main 函数
                if (funcDecl->entry->getName() == "main")
                {
                    this->mainExists = true;
                    // main 函数返回类型检查
                    if (funcDecl->retType != intType)
                    {
                        this->errors.push_back("Error: 'main' function must have return type 'int' at line " +
                                               std::to_string(funcDecl->line_num) + ".");
                        success = false;
                    }

                    // main 函数参数检查
                    if (funcDecl->params && !funcDecl->params->empty())
                    {
                        this->errors.push_back("Error: 'main' function must not have parameters at line " +
                                               std::to_string(funcDecl->line_num) + ".");
                        success = false;
                    }
                }
                // 对函数内部进行检查
                success = apply(*this, *funcDecl) && success;
            }
            else
            {
                // 非法顶层语句
                this->errors.push_back(
                    "Error: Invalid top-level statement at line " + std::to_string(stmt->line_num) + ".");
                success = false;
            }
        }

        if (!this->mainExists)
        {
            this->errors.push_back("Error: 'main' function is missing.");
            success = false;
        }
        return success;
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
