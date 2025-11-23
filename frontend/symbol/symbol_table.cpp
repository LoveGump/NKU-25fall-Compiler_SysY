#include <frontend/symbol/symbol_table.h>
#include <debug.h>

namespace FE::Sym
{
    // 重置符号表状态
    void SymTable::reset_impl()
    {
        // TODO("Lab3-1: Reset symbol table");
        this->globalTables_.clear();
        this->scopeStack_.clear();
        this->curDepth_ = -1;
    }

    // 进入一个新的作用域
    void SymTable::enterScope_impl()
    {
        // TODO("Lab3-1: Enter new scope");
        this->scopeStack_.emplace_back();
        this->curDepth_ += 1;
    }

    // 退出当前作用域
    void SymTable::exitScope_impl()
    {
        // TODO("Lab3-1: Exit current scope");
        ASSERT(curDepth_ >= 0 && "Exiting scope while at global scope");
        this->scopeStack_.pop_back();
        this->curDepth_ -= 1;
    }

    // 向符号表中添加一个符号及其属性
    void SymTable::addSymbol_impl(Entry* entry, FE::AST::VarAttr& attr)
    {
        // (void)entry;
        // (void)attr;
        // TODO("Lab3-1: Add symbol to current scope");
        ASSERT(entry && "Trying to add null entry to symbol table");
        if (this->isGlobalScope_impl())
        {
            // 在全局作用域中
            attr.scopeLevel = -1;
            auto result     = this->globalTables_.emplace(entry, attr);
            ASSERT(result.second && "Redefinition of symbol in global scope");
        }
        else
        {
            // 在局部作用域中
            attr.scopeLevel    = this->curDepth_;
            auto& currentScope = this->scopeStack_.back();  // 获取当前作用域的符号表
            auto  result       = currentScope.emplace(entry, attr);
            ASSERT(result.second && "Redefinition of symbol in local scope");
        }
    }

    // 检查全局符号表中对应entry的符号属性指针，找不到则返回nullptr
    FE::AST::VarAttr* SymTable::getSymbol_impl(Entry* entry)
    {
        // (void)entry;
        // TODO("Lab3-1: Get symbol from symbol table");
        ASSERT(entry && "Trying to get null entry from symbol table");
        // 先从局部作用域栈中查找，从内到外
        for (int d = this->curDepth_; d >= 0; --d)
        {
            auto& scope = this->scopeStack_[d];  // 获取当前作用域的符号表
            auto  it    = scope.find(entry);
            if (it != scope.end()) return &(it->second);
        }
        // 如果局部作用域中未找到，则在全局作用域中查找
        auto it = this->globalTables_.find(entry);
        if (it != this->globalTables_.end()) return &(it->second);
        return nullptr;  // 未找到
    }

    // 是否处于全局作用域
    bool SymTable::isGlobalScope_impl()
    {
        // TODO("Lab3-1: Check if current scope is global scope");
        return this->curDepth_ == -1;
    }

    int SymTable::getScopeDepth_impl()
    {
        // TODO("Lab3-1: Get current scope depth");
        return this->curDepth_;
    }
}  // namespace FE::Sym
