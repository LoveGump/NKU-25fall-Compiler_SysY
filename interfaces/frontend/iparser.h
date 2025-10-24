#ifndef __INTERFACES_FRONTEND_PARSER_IPARSER_H__
#define __INTERFACES_FRONTEND_PARSER_IPARSER_H__

#include <frontend/token.h>
#include <iostream>
#include <vector>

namespace FE
{
    namespace AST
    {
        class Root;
    }

    // 语法分析器模版基类
    template <typename Derived>
    class iParser
    {
      protected:
        std::istream* inStream;
        std::ostream* outStream;

      public:
        iParser(std::istream* inStream, std::ostream* outStream) : inStream(inStream), outStream(outStream) {}
        ~iParser() = default;

      public:
        void setInStream(std::istream* inStream) { this->inStream = inStream; }
        void setOutStream(std::ostream* outStream) { this->outStream = outStream; }

      public:
        // 词法分析接口
        std::vector<Token> parseTokens() { return static_cast<Derived*>(this)->parseTokens_impl(); }
        // 语法分析接口
        AST::Root* parseAST() { return static_cast<Derived*>(this)->parseAST_impl(); }
    };
}  // namespace FE

#endif  // __INTERFACES_FRONTEND_PARSER_IPARSER_H__
