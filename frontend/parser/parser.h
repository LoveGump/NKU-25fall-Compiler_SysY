#ifndef __FRONTEND_PARSER_PARSER_H__
#define __FRONTEND_PARSER_PARSER_H__

#include <frontend/iparser.h>
#include <frontend/parser/scanner.h>
#include <frontend/parser/yacc.h>

namespace FE
{
    class Parser : public iParser<Parser>
    {
        friend iParser<Parser>;// 让基类可以访问派生类的私有成员

      private:
        Scanner    _scanner;
        YaccParser _parser;

      public:
        AST::Root* ast;

      public:
        Parser(std::istream* inStream, std::ostream* outStream)
            : iParser<Parser>(inStream, outStream), _scanner(*this), _parser(_scanner, *this), ast(nullptr)
        {
            _scanner.switch_streams(inStream, outStream);
        }
        ~Parser() {}

        void reportError(const location& loc, const std::string& message);

      private:
        std::vector<Token> parseTokens_impl();
        AST::Root*         parseAST_impl();
    };
}  // namespace FE

#endif  // __FRONTEND_PARSER_PARSER_H__
