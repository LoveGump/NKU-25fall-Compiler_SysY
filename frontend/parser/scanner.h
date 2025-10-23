#ifndef __FRONTEND_PARSER_SCANNER_H__
#define __FRONTEND_PARSER_SCANNER_H__

#ifndef yyFlexLexerOnce
#undef yyFlexLexer
#define yyFlexLexer Yacc_FlexLexer
#include <FlexLexer.h>
#endif

#undef YY_DECL
#define YY_DECL FE::YaccParser::symbol_type FE::Scanner::nextToken()

#include <frontend/parser/yacc.h>

namespace FE
{
    class Parser;

    class Scanner : public yyFlexLexer
    {
      private:
        Parser& _parser;  // 引用 Parser 对象以便报告错误等

      public:
        Scanner(Parser& parser) : _parser(parser) {}  // 构造函数，初始化引用
        virtual ~Scanner() {}

        virtual YaccParser::symbol_type nextToken();  // 重载 nextToken 方法以返回 YaccParser 所需的符号类型
    };
}  // namespace FE

#endif  // __FRONTEND_PARSER_SCANNER_H__
