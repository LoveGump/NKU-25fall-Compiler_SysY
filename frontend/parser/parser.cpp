#include <frontend/parser/parser.h>
#include <debug.h>

namespace FE
{
    using type = YaccParser::symbol_type;
    using kind = YaccParser::symbol_kind;

    void Parser::reportError(const location& loc, const std::string& message) { _parser.error(loc, message); }

    std::vector<Token> Parser::parseTokens_impl()
    {
        std::vector<Token> tokens;
        while (true)
        {
            // 遇到终止token之前循环
            type token = _scanner.nextToken();
            if (token.kind() == kind::S_END) break;

            Token result;
            result.token_name    = token.name();
            result.line_number   = token.location.begin.line;
            result.column_number = token.location.begin.column - 1;
            result.lexeme        = _scanner.YYText();

            switch (token.kind())
            {
                case kind::S_INT_CONST:
                    result.ival = token.value.as<int>();
                    result.type = Token::TokenType::T_INT;
                    break;
                // 添加对LL_CONST和FLOAT_CONST的处理
                case kind::S_LL_CONST:
                    result.lval = token.value.as<long long>();
                    result.type = Token::TokenType::T_LL;
                    break;
                case kind::S_FLOAT_CONST:
                    result.fval = token.value.as<float>();
                    result.type = Token::TokenType::T_FLOAT;
                    break;
                // 标识符、单行注释、err 、字符串常量都使用同一种处理方式。
                // 因为它们的值都存储为std::string类型
                case kind::S_IDENT:
                case kind::S_SLASH_COMMENT:
                case kind::S_ERR_TOKEN:
                case kind::S_STR_CONST:
                    result.sval = token.value.as<std::string>();
                    result.type = Token::TokenType::T_STRING;
                    break;
                default: result.type = Token::TokenType::T_NONE; break;
            }

            tokens.push_back(result);
        }

        return tokens;
    }

    AST::Root* Parser::parseAST_impl()
    {
        _parser.parse();
        return ast;
    }
}  // namespace FE
