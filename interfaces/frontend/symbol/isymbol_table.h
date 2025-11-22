#ifndef __FRONTEND_SYMBOL_ISYMBOL_TABLE_H__
#define __FRONTEND_SYMBOL_ISYMBOL_TABLE_H__

#include <frontend/ast/ast_defs.h>
#include <frontend/symbol/symbol_entry.h>

namespace FE::Sym
{
    // 符号表接口类
    template <typename Derived>
    class iSymTable
    {
      public:
        iSymTable()          = default;
        virtual ~iSymTable() = default;

      public:
        // 重置符号表状态
        void reset() { static_cast<Derived*>(this)->reset_impl(); }

        // 向符号表中添加一个符号及其属性，重复就报错
        void addSymbol(Entry* entry, AST::VarAttr& attr) { static_cast<Derived*>(this)->addSymbol_impl(entry, attr); }
        // 返回符号表中对应entry的符号属性指针，找不到则返回nullptr
        AST::VarAttr* getSymbol(Entry* entry) { return static_cast<Derived*>(this)->getSymbol_impl(entry); }

        // 进入一个新的作用域。
        void enterScope() { static_cast<Derived*>(this)->enterScope_impl(); }
        // 退出当前作用域。
        void exitScope() { static_cast<Derived*>(this)->exitScope_impl(); }

        // 是否处于全局作用域
        bool isGlobalScope() { return static_cast<Derived*>(this)->isGlobalScope_impl(); }
        // 获取当前作用域深度
        int getScopeDepth() { return static_cast<Derived*>(this)->getScopeDepth_impl(); }
    };
}  // namespace FE::Sym

#endif  // __FRONTEND_SYMBOL_ISYMBOL_TABLE_H__
