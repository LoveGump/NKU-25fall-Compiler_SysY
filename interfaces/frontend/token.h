#ifndef __INTERFACES_FRONTEND_TOKEN_H__
#define __INTERFACES_FRONTEND_TOKEN_H__

#include <string>

namespace FE
{
    // 词法分析器生成的 Token 结构体
    struct Token
    {
        std::string token_name;     ///< 词法分析中使用的 token 名称
        std::string lexeme;         ///< 该 token 的原始文本内容
        int         line_number;    ///< 该 token 所在的行号
        int         column_number;  ///< 该 token 所在的列号

        enum class TokenType
        {
            T_INT,
            T_LL,
            T_FLOAT,
            T_DOUBLE,
            T_STRING,
            T_NONE
        } type;
        union
        {
            int       ival;
            long long lval;
            float     fval;
            double    dval;
        };
        std::string sval;
    };
}  // namespace FE

#endif  // __INTERFACES_FRONTEND_TOKEN_H__
