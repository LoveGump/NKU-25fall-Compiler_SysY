#ifndef __FRONTEND_SYMBOL_SYMBOL_TABLE_H__
#define __FRONTEND_SYMBOL_SYMBOL_TABLE_H__

#include <frontend/symbol/isymbol_table.h>
#include <vector>
#include <map>

namespace FE::Sym
{
    // 符号表的具体实现
    class SymTable : public iSymTable<SymTable>
    {
        friend iSymTable<SymTable>;

      private:
        // 全局作用域符号集合（scopeLevel = -1）
        std::map<Entry*, FE::AST::VarAttr> glbSymbols_;
        // 局部作用域栈，每个元素为当前层的符号集合
        std::vector<std::map<Entry*, FE::AST::VarAttr>> scopeStack_;
        // 当前作用域深度：-1 表示处于全局作用域；0、1、... 为局部层级 方便便利
        int curDepth_ = -1;

      public:
        SymTable()  = default;
        ~SymTable() = default;

        void reset_impl();

        void              addSymbol_impl(Entry* entry, FE::AST::VarAttr& attr);
        FE::AST::VarAttr* getSymbol_impl(Entry* entry);
        void              enterScope_impl();
        void              exitScope_impl();

        bool isGlobalScope_impl();
        int  getScopeDepth_impl();
    };
}  // namespace FE::Sym

#endif  // __FRONTEND_SYMBOL_SYMBOL_TABLE_H__
