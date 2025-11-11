#include <frontend/symbol/symbol_table.h>
#include <debug.h>

namespace FE::Sym
{
    void SymTable::reset_impl()
    {
        // TODO("Lab3-1: Reset symbol table");
        // 清空全局符号和作用域栈，重置深度
        glbSymbols_.clear();
        scopeStack_.clear();
        curDepth_ = -1;
    }

    void SymTable::enterScope_impl()
    {
        // TODO("Lab3-1: Enter new scope");
        // 在作用域栈中添加新的一层，深度加一
        scopeStack_.emplace_back();
        ++curDepth_;
    }

    void SymTable::exitScope_impl()
    {
        //  TODO("Lab3-1: Exit current scope");
        // 确保不在全局作用域下退出作用域
        ASSERT(curDepth_ >= 0 && "Exiting scope while at global scope");
        // 从作用域栈中移除当前层，深度减一
        scopeStack_.pop_back();
        --curDepth_;
    }

    void SymTable::addSymbol_impl(Entry* entry, FE::AST::VarAttr& attr)
    {
        // 确保entry不为空
        ASSERT(entry && "Null Entry when adding symbol");
        // (void)entry;
        // (void)attr;
        // TODO("Lab3-1: Add symbol to current scope");
        // 根据当前作用域深度将符号添加到相应的符号集合中
        if (isGlobalScope_impl())
        {
            // 全局作用域
            attr.scopeLevel    = -1;
            glbSymbols_[entry] = attr;
        }
        else
        {
            // 局部作用域
            ASSERT(!scopeStack_.empty());
            attr.scopeLevel  = curDepth_;
            auto& currScope  = scopeStack_.back();  //栈顶
            currScope[entry] = attr;
        }
    }

    FE::AST::VarAttr* SymTable::getSymbol_impl(Entry* entry)
    {
        // (void)entry;
        // TODO("Lab3-1: Get symbol from symbol table");
        // 确保entry不为空
        ASSERT(entry && "Null Entry when querying symbol");

        for (int d = curDepth_; d >= 0; --d)
        {
            // 从当前作用域向上查找符号
            auto& scope = scopeStack_[static_cast<size_t>(d)];
            auto  it    = scope.find(entry);
            if (it != scope.end()) return &it->second;
        }

        // 最后查找全局作用域
        auto it = glbSymbols_.find(entry);
        if (it != glbSymbols_.end()) return &it->second;

        return nullptr;
    }

    bool SymTable::isGlobalScope_impl()
    {
        // TODO("Lab3-1: Check if current scope is global scope");
        return curDepth_ < 0;
    }

    int SymTable::getScopeDepth_impl()
    {
        //  TODO("Lab3-1: Get current scope depth");
        return curDepth_;
    }
}  // namespace FE::Sym
