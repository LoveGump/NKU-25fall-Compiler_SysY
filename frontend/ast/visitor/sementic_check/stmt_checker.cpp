#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(ExprStmt& node)
    {
        // 示例实现：表达式语句的语义检查
        // 空表达式直接通过，否则访问内部表达式
        if (!node.expr) return true;
        return apply(*this, *node.expr);
    }

    bool ASTChecker::visit(FuncDeclStmt& node)
    {
        // TODO(Lab3-1): 实现函数声明的语义检查
        // 检查作用域，记录函数信息，处理形参和函数体，检查返回语句
        //(void)node;
        //TODO("Lab3-1: Implement FuncDeclStmt semantic checking");
        // 函数声明的语义检查
        bool accept = true;
        
        // 检查是否在全局作用域
        if (!symTable.isGlobalScope()) {
            errors.push_back("Error: function declaration must be at global scope at line " + 
                           std::to_string(node.line_num));
            accept = false;
        }
        
        // 检查函数是否重定义
        if (funcDecls.find(node.entry) != funcDecls.end()) {
            errors.push_back("Error: redefinition of function '" + 
                           node.entry->getName() + "' at line " + 
                           std::to_string(node.line_num));
            accept = false;
        }
        
        // 记录函数信息
        funcDecls[node.entry] = &node;
        
        // 检查是否是main函数
        if (node.entry->getName() == "main") {
            mainExists = true;
            // main函数必须返回int
            if (node.retType->getBaseType() != Type_t::INT) {
                errors.push_back("Error: main function must return int at line " + 
                               std::to_string(node.line_num));
                accept = false;
            }
        }
        
        // 保存当前函数返回类型
        Type* prevFuncRetType = curFuncRetType;
        bool prevFuncHasReturn = funcHasReturn;
        
        curFuncRetType = node.retType;
        funcHasReturn = false;
        
        // 进入函数作用域
        symTable.enterScope();
        
        // 处理形参
        if (node.params) {
            for (auto* param : *(node.params)) {
                if (!param) continue;
                accept &= apply(*this, *param);
            }
        }
        
        // 访问函数体
        if (node.body) {
            accept &= apply(*this, *node.body);
        }
        
        // 检查返回语句
        if (node.retType->getBaseType() != Type_t::VOID && !funcHasReturn) {
            errors.push_back("Warning: non-void function '" + node.entry->getName() + 
                           "' may not return a value at line " + 
                           std::to_string(node.line_num));
        }
        
        // 退出函数作用域
        symTable.exitScope();
        
        // 恢复之前的函数返回类型
        curFuncRetType = prevFuncRetType;
        funcHasReturn = prevFuncHasReturn;
        
        return accept;
    }

    bool ASTChecker::visit(VarDeclStmt& node)
    {
        // TODO(Lab3-1): 实现变量声明语句的语义检查
        // 空声明直接通过，否则委托给变量声明处理
        //(void)node;
        //TODO("Lab3-1: Implement VarDeclStmt semantic checking");
        // 变量声明语句的语义检查
        if (!node.decl) return true;
        return apply(*this, *node.decl);
    }

    bool ASTChecker::visit(BlockStmt& node)
    {
        // TODO(Lab3-1): 实现块语句的语义检查
        // 进入新作用域，逐条访问语句，退出作用域
        //(void)node;
        //TODO("Lab3-1: Implement BlockStmt semantic checking");
        // 块语句的语义检查
        bool accept = true;
        
        // 进入新作用域
        symTable.enterScope();
        
        // 逐条访问语句
        if (node.stmts) {
            for (auto* stmt : *(node.stmts)) {
                if (!stmt) continue;
                accept &= apply(*this, *stmt);
            }
        }
        
        // 退出作用域
        symTable.exitScope();
        
        return accept;
    }

    bool ASTChecker::visit(ReturnStmt& node)
    {
        // TODO(Lab3-1): 实现返回语句的语义检查
        // 设置返回标记，检查作用域，检查返回值类型匹配
        //(void)node;
        //TODO("Lab3-1: Implement ReturnStmt semantic checking");
        // 返回语句的语义检查
        bool accept = true;
        
        // 设置返回标记
        funcHasReturn = true;
        
        // 检查是否在函数内
        if (symTable.isGlobalScope()) {
            errors.push_back("Error: return statement outside function at line " + 
                           std::to_string(node.line_num));
            return false;
        }
        
        // 访问返回值表达式
        if (node.retExpr) {
            accept &= apply(*this, *node.retExpr);
            
            // 检查返回值类型
            Type* retType = node.retExpr->attr.val.value.type;
            if (curFuncRetType->getBaseType() == Type_t::VOID) {
                errors.push_back("Error: void function should not return a value at line " + 
                               std::to_string(node.line_num));
                accept = false;
            } else if (retType != curFuncRetType) {
                // 允许一些隐式转换
                if (!(retType->getBaseType() == Type_t::INT && curFuncRetType->getBaseType() == Type_t::FLOAT) &&
                    !(retType->getBaseType() == Type_t::FLOAT && curFuncRetType->getBaseType() == Type_t::INT) &&
                    !(retType->getBaseType() == Type_t::BOOL && curFuncRetType->getBaseType() == Type_t::INT)) {
                    errors.push_back("Warning: return type mismatch at line " + 
                                   std::to_string(node.line_num));
                }
            }
        } else {
            // 没有返回值
            if (curFuncRetType->getBaseType() != Type_t::VOID) {
                errors.push_back("Error: non-void function must return a value at line " + 
                               std::to_string(node.line_num));
                accept = false;
            }
        }
        
        return accept;
    }

    bool ASTChecker::visit(WhileStmt& node)
    {
        // TODO(Lab3-1): 实现while循环的语义检查
        // 检查作用域，访问条件表达式，管理循环深度，访问循环体
        //(void)node;
        //TODO("Lab3-1: Implement WhileStmt semantic checking");
        // while循环的语义检查
    bool accept = true;
    
    // 检查是否在全局作用域
    if (symTable.isGlobalScope()) {
        errors.push_back("Error: while statement at global scope at line " + 
                       std::to_string(node.line_num));
        accept = false;
    }
    
    // 访问条件表达式
    if (node.cond) {
        accept &= apply(*this, *node.cond);
    }
    
    // 管理循环深度
    loopDepth++;
    
    // 访问循环体
    if (node.body) {
        accept &= apply(*this, *node.body);
    }
    
    // 恢复循环深度
    loopDepth--;
    
    return accept;
    }

    bool ASTChecker::visit(IfStmt& node)
    {
        // TODO(Lab3-1): 实现if语句的语义检查
        // 检查作用域，访问条件表达式，分别访问then和else分支
        //(void)node;
        //TODO("Lab3-1: Implement IfStmt semantic checking");
        // if语句的语义检查
        bool accept = true;
        
        // 检查是否在全局作用域
        if (symTable.isGlobalScope()) {
            errors.push_back("Error: if statement at global scope at line " + 
                           std::to_string(node.line_num));
            accept = false;
        }
        
        // 访问条件表达式
        if (node.cond) {
            accept &= apply(*this, *node.cond);
        }
        
        // 访问then分支
        if (node.thenStmt) {
            accept &= apply(*this, *node.thenStmt);
        }
        
        // 访问else分支（如果有）
        if (node.elseStmt) {
            accept &= apply(*this, *node.elseStmt);
        }
        
        return accept;
    }

    bool ASTChecker::visit(BreakStmt& node)
    {
        // TODO(Lab3-1): 实现break语句的语义检查
        // 检查是否在循环内使用
        //(void)node;
        //TODO("Lab3-1: Implement BreakStmt semantic checking");
        // break语句的语义检查
    bool accept = true;
    
    // 检查是否在循环内使用
    if (loopDepth == 0) {
        errors.push_back("Error: break statement not within loop at line " + 
                       std::to_string(node.line_num));
        accept = false;
    }
    
    return accept;
    }

    bool ASTChecker::visit(ContinueStmt& node)
    {
        // TODO(Lab3-1): 实现continue语句的语义检查
        // 检查是否在循环内使用
        //(void)node;
        //TODO("Lab3-1: Implement ContinueStmt semantic checking");
        // continue语句的语义检查
    bool accept = true;
    
    // 检查是否在循环内使用
    if (loopDepth == 0) {
        errors.push_back("Error: continue statement not within loop at line " + 
                       std::to_string(node.line_num));
        accept = false;
    }
    
    return accept;
    }

    bool ASTChecker::visit(ForStmt& node)
    {
        // TODO(Lab3-1): 实现for循环的语义检查
        // 检查作用域，访问初始化、条件、步进表达式，管理循环深度
        //(void)node;
        //TODO("Lab3-1: Implement ForStmt semantic checking");

         // for循环的语义检查
    bool accept = true;
    
    // 检查是否在全局作用域
    if (symTable.isGlobalScope()) {
        errors.push_back("Error: for statement at global scope at line " + 
                       std::to_string(node.line_num));
        accept = false;
    }
    
    // 进入新作用域（for循环的初始化变量有自己的作用域）
    symTable.enterScope();
    
    // 访问初始化表达式
    if (node.init) {
        accept &= apply(*this, *node.init);
    }
    
    // 访问条件表达式
    if (node.cond) {
        accept &= apply(*this, *node.cond);
    }
    
    // 访问步进表达式
    if (node.step) {
        accept &= apply(*this, *node.step);
    }
    
    // 管理循环深度
    loopDepth++;
    
    // 访问循环体
    if (node.body) {
        accept &= apply(*this, *node.body);
    }
    
    // 恢复循环深度
    loopDepth--;
    
    // 退出作用域
    symTable.exitScope();
    
    return accept;
    }
}  // namespace FE::AST
