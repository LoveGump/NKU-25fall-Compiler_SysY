#include <frontend/ast/visitor/sementic_check/ast_checker.h>
#include <debug.h>

namespace FE::AST
{
    bool ASTChecker::visit(LeftValExpr& node)
    {
        // TODO(Lab3-1): 实现左值表达式的语义检查
        // 检查变量是否存在，处理数组下标访问，进行类型检查和常量折叠
        //(void)node;
        //TODO("Lab3-1: Implement LeftValExpr semantic checking");
        // 左值表达式的语义检查
        bool accept = true;
        
        // 检查变量是否存在
        auto* symbol = symTable.getSymbol(node.entry);
        if (!symbol) {
            errors.push_back("Error: undefined variable '" + 
                           node.entry->getName() + "' at line " + 
                           std::to_string(node.line_num));
            node.attr.val.value.type = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }
        
        // 设置变量类型
        node.attr.val.value.type = symbol->type;
        
        // 处理数组下标访问
        if (node.indices && !node.indices->empty()) {
            // 检查是否真的是数组
            if (symbol->arrayDims.empty() && symbol->type->getTypeGroup() != TypeGroup::POINTER) {
                errors.push_back("Error: subscripted value is not an array at line " + 
                               std::to_string(node.line_num));
                accept = false;
            }
            
            // 检查每个下标表达式
            bool allConstexpr = true;
            for (auto* dimExpr : *(node.indices)) {
                if (!dimExpr) continue;
                accept &= apply(*this, *dimExpr);
                
                // 下标必须是整数类型
                if (dimExpr->attr.val.value.type->getBaseType() != Type_t::INT &&
                    dimExpr->attr.val.value.type->getBaseType() != Type_t::BOOL) {
                    errors.push_back("Error: array subscript is not an integer at line " + 
                                   std::to_string(node.line_num));
                    accept = false;
                }
                
                if (!dimExpr->attr.val.isConstexpr) {
                    allConstexpr = false;
                }
            }
            
            // 数组访问后不再是常量表达式（除非是编译期可确定的）
            if (symbol->isConstDecl && allConstexpr && !symbol->initList.empty()) {
                // 可以尝试常量折叠
                node.attr.val.isConstexpr = true;
                // 这里简化处理，实际需要计算偏移量
                if (!symbol->initList.empty()) {
                    node.attr.val.value = symbol->initList[0];
                }
            } else {
                node.attr.val.isConstexpr = false;
            }
        } else {
            // 不是数组访问
            if (symbol->isConstDecl && !symbol->initList.empty()) {
                // 常量变量，可以使用其值
                node.attr.val.isConstexpr = true;
                node.attr.val.value = symbol->initList[0];
            } else {
                node.attr.val.isConstexpr = false;
            }
        }
        
        return accept;
    }

    bool ASTChecker::visit(LiteralExpr& node)
    {
        // 示例实现：字面量表达式的语义检查
        // 字面量总是编译期常量，直接设置属性
        node.attr.val.isConstexpr = true;
        node.attr.val.value       = node.literal;
        return true;
    }

    bool ASTChecker::visit(UnaryExpr& node)
    {
        // TODO(Lab3-1): 实现一元表达式的语义检查
        // 访问子表达式，检查操作数类型，调用类型推断函数
        //(void)node;
        //TODO("Lab3-1: Implement UnaryExpr semantic checking");
        // 一元表达式的语义检查
        bool accept = true;
        
        // 访问子表达式
        if (!node.expr) {
            errors.push_back("Error: unary expression missing operand at line " + 
                           std::to_string(node.line_num));
            return false;
        }
        
        accept &= apply(*this, *node.expr);
        
        // 检查操作数类型
        Type* operandType = node.expr->attr.val.value.type;
        if (operandType == voidType) {
            errors.push_back("Error: invalid operand type for unary operator at line " + 
                           std::to_string(node.line_num));
            node.attr.val.value.type = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }
        
        // 调用类型推断函数
        bool hasError = false;
        node.attr.val = typeInfer(node.expr->attr.val, node.op, node, hasError);
        
        if (hasError) {
            accept = false;
        }
        
        return accept;
    }

    bool ASTChecker::visit(BinaryExpr& node)
    {
        // TODO(Lab3-1): 实现二元表达式的语义检查
        // 访问左右子表达式，检查操作数类型，调用类型推断
        //(void)node;
        //TODO("Lab3-1: Implement BinaryExpr semantic checking");
        // 二元表达式的语义检查
        bool accept = true;
        
        // 访问左右子表达式
        if (!node.lhs || !node.rhs) {
            errors.push_back("Error: binary expression missing operand at line " + 
                           std::to_string(node.line_num));
            return false;
        }
        
        accept &= apply(*this, *node.lhs);
        accept &= apply(*this, *node.rhs);
        
        // 检查操作数类型
        Type* lhsType = node.lhs->attr.val.value.type;
        Type* rhsType = node.rhs->attr.val.value.type;
        
        if (lhsType == voidType || rhsType == voidType) {
            errors.push_back("Error: invalid operand type for binary operator at line " + 
                           std::to_string(node.line_num));
            node.attr.val.value.type = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }
        
        // 调用类型推断
        bool hasError = false;
        node.attr.val = typeInfer(node.lhs->attr.val, node.rhs->attr.val, node.op, node, hasError);
        
        if (hasError) {
            accept = false;
        }
        
        return accept;
    }

    bool ASTChecker::visit(CallExpr& node)
    {
        // TODO(Lab3-1): 实现函数调用表达式的语义检查
        // 检查函数是否存在，访问实参列表，检查参数数量和类型匹配
        //(void)node;
        //TODO("Lab3-1: Implement CallExpr semantic checking");
        // 函数调用表达式的语义检查
        bool accept = true;
        
        // 检查函数是否存在
        auto funcIt = funcDecls.find(node.func);
        if (funcIt == funcDecls.end()) {
            errors.push_back("Error: undefined function '" + 
                           node.func->getName() + "' at line " + 
                           std::to_string(node.line_num));
            node.attr.val.value.type = voidType;
            node.attr.val.isConstexpr = false;
            return false;
        }
        
        FuncDeclStmt* funcDecl = funcIt->second;
        
        // 设置返回类型
        node.attr.val.value.type = funcDecl->retType;
        node.attr.val.isConstexpr = false;  // 函数调用不是常量表达式
        
        // 访问实参列表
        size_t argCount = 0;
        if (node.args) {
            for (auto* arg : *(node.args)) {
                if (!arg) continue;
                accept &= apply(*this, *arg);
                argCount++;
            }
        }
        
        // 检查参数数量
        size_t paramCount = 0;
        if (funcDecl->params) {
            paramCount = funcDecl->params->size();
        }
        
        if (argCount != paramCount) {
            errors.push_back("Error: function '" + node.func->getName() + 
                           "' expects " + std::to_string(paramCount) + 
                           " arguments, but " + std::to_string(argCount) + 
                           " provided at line " + std::to_string(node.line_num));
            accept = false;
        }
        
        // 检查参数类型匹配（简化版本）
        if (node.args && funcDecl->params && argCount == paramCount) {
            for (size_t i = 0; i < argCount; i++) {
                auto* arg = (*(node.args))[i];
                auto* param = (*(funcDecl->params))[i];
                
                Type* argType = arg->attr.val.value.type;
                Type* paramType = param->attr.val.value.type;
                
                // 简单的类型检查（可以扩展）
                if (argType->getBaseType() != paramType->getBaseType() &&
                    argType->getTypeGroup() != paramType->getTypeGroup()) {
                    // 允许一些隐式转换
                    if (!(argType->getBaseType() == Type_t::INT && paramType->getBaseType() == Type_t::FLOAT) &&
                        !(argType->getBaseType() == Type_t::FLOAT && paramType->getBaseType() == Type_t::INT)) {
                        errors.push_back("Warning: type mismatch for argument " + 
                                       std::to_string(i + 1) + " in function call at line " + 
                                       std::to_string(node.line_num));
                    }
                }
            }
        }
        
        return accept;
    }

    bool ASTChecker::visit(CommaExpr& node)
    {
        // TODO(Lab3-1): 实现逗号表达式的语义检查
        // 依序访问各子表达式，结果为最后一个表达式的属性
        //(void)node;
        //TODO("Lab3-1: Implement CommaExpr semantic checking");
         // 逗号表达式的语义检查
         bool accept = true;
        
         if (!node.exprs || node.exprs->empty()) {
             return true;
         }
         
         // 依序访问各子表达式
         for (auto* expr : *(node.exprs)) {
             if (!expr) continue;
             accept &= apply(*this, *expr);
         }
         
         // 结果为最后一个表达式的属性
         if (!node.exprs->empty()) {
             auto* lastExpr = node.exprs->back();
             if (lastExpr) {
                 node.attr = lastExpr->attr;
             }
         }
         
         return accept;
    }
}  // namespace FE::AST
