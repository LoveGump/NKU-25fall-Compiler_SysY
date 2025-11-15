#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(Initializer& node)
    {
        // 示例实现：单个初始化器的语义检查
        // 1) 访问初始化值表达式
        // 2) 将子表达式的属性拷贝到当前节点
        ASSERT(node.init_val && "Null initializer value");
        bool res  = apply(*this, *node.init_val);
        node.attr = node.init_val->attr;
        return res;
    }

    bool ASTChecker::visit(InitializerList& node)
    {
        // 示例实现：初始化器列表的语义检查
        // 遍历列表中的每个初始化器并逐个访问
        if (!node.init_list) return true;
        bool res = true;
        for (auto* init : *(node.init_list))
        {
            if (!init) continue;
            res &= apply(*this, *init);
        }
        return res;
    }

    bool ASTChecker::visit(VarDeclarator& node)
    {
        // TODO(Lab3-1): 实现变量声明器的语义检查
        // 访问左值表达式，同步属性，处理初始化器（如果有）
        //(void)node;
        //TODO("Lab3-1: Implement VarDeclarator semantic checking");
        // 变量声明器的语义检查
        bool accept = true;
        
        // 访问左值表达式（变量名）
        if (node.lval) {
            accept &= apply(*this, *node.lval);
            // 同步属性
            node.attr = node.lval->attr;
        }
        
        // 处理初始化器（如果有）
        if (node.init) {
            accept &= apply(*this, *node.init);
            
            // 检查初始化器类型是否匹配
            // 注意：VarDeclarator的isConstDecl信息需要从VarDeclaration传递过来
            // 这里暂时不做检查，由VarDeclaration处理
        }
        
        return accept;
    }

    bool ASTChecker::visit(ParamDeclarator& node)
    {
        // TODO(Lab3-1): 实现函数形参的语义检查
        // 检查形参重定义，处理数组形参的类型退化，将形参加入符号表
        //(void)node;
        //TODO("Lab3-1: Implement ParamDeclarator semantic checking");
        // 函数形参的语义检查
        bool accept = true;
        
        // 检查形参是否重定义
        if (symTable.getSymbol(node.entry)) {
            errors.push_back("Error: redefinition of parameter '" + 
                           node.entry->getName() + "' at line " + 
                           std::to_string(node.line_num));
            accept = false;
        }
        
        // 创建形参属性
        VarAttr paramAttr;
        paramAttr.type = node.type;
        paramAttr.isConstDecl = false;
        paramAttr.scopeLevel = symTable.getScopeDepth();
        
        // 处理数组形参的类型退化（数组退化为指针）
        if (node.dims && !node.dims->empty()) {
            paramAttr.type = TypeFactory::getPtrType(node.type);
            // 保存维度信息
            for (auto* dim : *node.dims) {
                if (dim) {
                    accept &= apply(*this, *dim);
                }
            }
        }
        
        // 设置节点的值类型（用于后续代码生成）
        node.attr.val.value.type = paramAttr.type;
        
        // 将形参加入符号表
        symTable.addSymbol(node.entry, paramAttr);
        
        return accept;
    }

    bool ASTChecker::visit(VarDeclaration& node)
    {
        // TODO(Lab3-1): 实现变量声明的语义检查
        // 遍历声明列表，检查重定义，处理数组维度和初始化，将符号加入符号表
        //(void)node;
        //TODO("Lab3-1: Implement VarDeclaration semantic checking");
        // 变量声明的语义检查
        bool accept = true;
        
        if (!node.decls) return true;
        
        // 遍历声明列表
        for (auto* decl : *(node.decls)) {
            if (!decl) continue;
            
            // 获取左值表达式（必须是LeftValExpr）
            auto* lval = dynamic_cast<LeftValExpr*>(decl->lval);
            if (!lval) continue;
            
            // 检查变量是否重定义（在当前作用域）
            auto* existingSymbol = symTable.getSymbol(lval->entry);
            if (existingSymbol && existingSymbol->scopeLevel == symTable.getScopeDepth()) {
                errors.push_back("Error: redefinition of variable '" + 
                               lval->entry->getName() + "' at line " + 
                               std::to_string(decl->line_num));
                accept = false;
                continue;
            }
            
            // 创建变量属性
            VarAttr varAttr;
            varAttr.type = node.type;
            varAttr.isConstDecl = node.isConstDecl;
            varAttr.scopeLevel = symTable.getScopeDepth();
            
            // 处理数组维度
            if (lval->indices && !lval->indices->empty()) {
                // 检查数组维度是否为正整数常量
                for (auto* dimExpr : *(lval->indices)) {
                    if (!dimExpr) {
                        errors.push_back("Error: array dimension cannot be empty at line " + 
                                       std::to_string(decl->line_num));
                        accept = false;
                        continue;
                    }
                    
                    accept &= apply(*this, *dimExpr);
                    
                    if (!dimExpr->attr.val.isConstexpr) {
                        errors.push_back("Error: array dimension must be constant at line " + 
                                       std::to_string(decl->line_num));
                        accept = false;
                    } else {
                        int dimValue = dimExpr->attr.val.getInt();
                        if (dimValue <= 0) {
                            errors.push_back("Error: array dimension must be positive at line " + 
                                           std::to_string(decl->line_num));
                            accept = false;
                        }
                        varAttr.arrayDims.push_back(dimValue);
                    }
                }
            }
            
            // 设置左值表达式的属性（用于后续）
            lval->attr.val.value.type = varAttr.type;
            
            // 访问声明器（处理初始化）
            accept &= apply(*this, *decl);
            
            // 如果有初始化器，处理初始化列表
            if (decl->init) {
                // 检查常量声明必须用常量表达式初始化
                if (node.isConstDecl && !decl->init->attr.val.isConstexpr) {
                    errors.push_back("Error: const variable must be initialized with constant expression at line " + 
                                   std::to_string(decl->line_num));
                    accept = false;
                }
                
                // 对于常量声明，将初始化值保存到属性中
                if (node.isConstDecl && decl->init->attr.val.isConstexpr) {
                    varAttr.initList.push_back(decl->init->attr.val.value);
                    decl->attr.val = decl->init->attr.val;
                }
            } else {
                // 常量声明必须有初始化器
                if (node.isConstDecl) {
                    errors.push_back("Error: const variable must be initialized at line " + 
                                   std::to_string(decl->line_num));
                    accept = false;
                }
            }
            
            // 将符号加入符号表
            symTable.addSymbol(lval->entry, varAttr);
            
            // 如果是全局变量，也加入全局符号表
            if (symTable.isGlobalScope()) {
                glbSymbols[lval->entry] = varAttr;
            }
        }
        
        return accept;
    }
}  // namespace FE::AST
