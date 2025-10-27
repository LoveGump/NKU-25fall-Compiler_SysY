%{
    #include <frontend/parser/yacc.h>
    #include <frontend/parser/scanner.h>
    #include <frontend/parser/location.hh>
    #include <frontend/parser/parser.h>
    #include <stdexcept>
    #include <string>
    #include <limits>      
    #include <cmath>       
    #include <algorithm>

    using namespace FE;

    static location loc;
    // loc记录了一个token的起始地址和终止地址

    // YY_USER_ACTION 宏会自动插入到每个规则的代码块开头
    // 如下方的 "int"               { RETT(INT, loc) }
    // 实际上会被转换为
    // "int"               { loc.step(); loc.columns(yyleng); RETT(INT, loc) }
    // 这样就能自动更新 loc 的位置
    // yyleng 是当前匹配的字符串的长度
    // loc.step() 用于将位置向前移动一个字符
    #define YY_USER_ACTION      \
        loc.step();             \
        loc.columns(yyleng);

    // yywrap()是Flex在读到文件结尾时自动调用的函数
    // 返回1表示文件结束，返回0表示还有内容需要处理
    // 这里我们直接返回1，表示文件结束省略了yywrap的实现
    #define yywrap() 1  
    #define RETT(x, y) \
        return YaccParser::make_##x(y);  // 该宏用于简化返回 token 的代码

    #define TAB_WIDTH 4 // 定义制表符的宽度

    int handleTab(); // 处理制表符的函数声明
    long long convertToInt(const char* str, const char end, bool& isLongLong); // 字符串转整数函数声明
    float convertToFloatDec(const char* str); // 字符串转十进制浮点数函数声明
    float convertToFloatHex(const char* str); // 字符串转十六进制浮点数函数声明
%}
/* 生成 C++ 类扫描器 */
%option c++
/* 不需要yywrap() */
%option noyywrap debug
/* 扫描器类名为 Scanner，前缀为 Yacc_ */
%option yyclass="Scanner"
%option prefix="Yacc_"

%%

%{
/*
在词法分析中，你需要重点考虑result的四种类型，分别为int_token, float_token, string_token, symbol_token。
之所以要定义这四种类型，是因为我们只返回这个token的类型是不够的，我们还需要知道这个token的属性。
例如我们需要知道INT_CONST具体的值是多少。
如果你需要其它 token，需要先到 yacc.y 中定义 token，然后在这里实现。

TODO(Lab1)：参考SysY2022的定义，实现更多的词法，如浮点数、十六进制/八进制数等
*/
%}


\n              {   
                // 处理换行符，更新行号和列号 yyleng = len(yytext) = 1
                // 这里yytext = "\n" 属于一个字符
                    loc.lines(yyleng);
                    loc.step();
                }

[ \f\r\v]+    { /* empty ，这三个基本没用 \f :分页 \r 换行符 和\n一起用  \v 纵向制表符 */ }
[\t]    {
    loc.columns(handleTab());
}
    %{/* 这些是关键字 */%}
"if"                { RETT(IF, loc) }
"else"              { RETT(ELSE, loc) }
"for"               { RETT(FOR, loc) }
"while"             { RETT(WHILE, loc) }
"continue"          { RETT(CONTINUE, loc) }
"break"             { RETT(BREAK, loc) }
"switch"            { RETT(SWITCH, loc) }
"case"              { RETT(CASE, loc) }
"goto"              { RETT(GOTO, loc) }
"do"                { RETT(DO, loc) }
"return"            { RETT(RETURN, loc) }

"const"             { RETT(CONST, loc) }

";"                 { RETT(SEMICOLON, loc) }
","                 { RETT(COMMA, loc) }

    %{ /* 这里对单行、多行注释的处理逻辑 */ %}
"//".*              { 
    auto str = std::string(yytext);
    // return YaccParser::make_SLASH_COMMENT(str, loc);  //思考为什么要把这一行注释掉
    // 在NKU-Compiler2025/frontend/parser/yacc.y  202 有对单行注释的处理
    // 这里注释掉是因为单行注释不需要作为token返回给parser
}


    %{ /* 这里对多行注释的处理逻辑 
        我来解释一下这个正则表达式的含义：
        “/ *”           匹配注释的开始符号 "/ *"
        ([^*]         匹配任意不是 '*' 的字符
        |            或者
        \ *+ [^/]     匹配一个或多个 '*' 后跟一个不是 '/' 的字符
        )*           上述两种情况可以重复零次或多次
        ”* /“          匹配注释的结束符号 "* / "
        因为Flex的处理逻辑 是贪婪匹配的，所以它会尽可能多地匹配字符，直到遇到第一个符合结束符号的地方为止。
        这样就能正确处理多行注释，即使注释中间包含换行符也不会影响匹配。
        /\*([^*]|\*+[^* / ] ) * \ * /
        */ %}

"/*"([^*]|\*[^/])*"*/" {
    //TODO(Lab1)：实现多行注释的处理，包括修改上方的匹配规则
    auto str = std::string(yytext); // 获取匹配的注释字符串
    loc.lines(std::count(str.begin(), str.end(), '\n')); // 更新行号
    // return YaccParser::make_MULTI_COMMENT(str, loc); 
    // 多行注释也是不把其作为token
}

%{/*reference: https://github.com/CentaureaHO/SysY_Compiler_2025/blob/main/parser/lexer.l : 105-115*/ %}
"/*"  {
    // 处理未闭合的多行注释
    auto str = std::string(yytext); // 获取匹配的注释字符串
    return YaccParser::make_ERR_TOKEN(str, loc);
}
"*/"  {
    // 处理多余的闭合注释符号
    auto str = std::string(yytext); // 获取匹配的注释字符串
    return YaccParser::make_ERR_TOKEN(str, loc);
}


    %{/* 类型的定义 */%}
"int"      { RETT(INT, loc)  }
"float"     {RETT(FLOAT,loc ) }
"void"    { RETT(VOID, loc)  }  

    %{ /* 运算符的定义 */ %}
"+"       { RETT(PLUS, loc); }
"-"       { RETT(MINUS, loc); }
"*"       { RETT(STAR, loc); }
"/"       { RETT(SLASH, loc); }
"%"       { RETT(MOD, loc); }

"=="      { RETT(EQ, loc); }
"!="      { RETT(NE, loc); }
"<="      { RETT(LE, loc); }
">="      { RETT(GE, loc); }
"<"       { RETT(LT, loc); }
">"       { RETT(GT, loc); }

    %{ /* 这里是逻辑运算符的定义 */ %}
"&&"      { RETT(AND, loc); }
"||"      { RETT(OR, loc); }
"!"       { RETT(NOT, loc); }

"="       { RETT(ASSIGN, loc); }
"+="      { RETT(PLUS_ASSIGN, loc); }
"-="      { RETT(MINUS_ASSIGN, loc); }
"*="      { RETT(MUL_ASSIGN, loc); }
"/="      { RETT(DIV_ASSIGN, loc); }
"%="      { RETT(MOD_ASSIGN, loc); }

":"       { RETT(COLON, loc); }



    %{ /* 这里是浮点数的处理逻辑 
        前面是十进制浮点数的处理，因为这里正则的可靠性较高，所以不需要try-catch
        后面是十六进制浮点数的处理
    */ %}
[0-9]+"."[0-9]*([eE][-+]?[0-9]+)? {
    // 对十进制浮点数的处理
    // ([eE][-+]?[0-9]+)?：匹配科学计数法部分
    try {
        float fval = convertToFloatDec(yytext);
        return YaccParser::make_FLOAT_CONST(fval, loc);// 多参数 不匹配宏 
    } catch (const std::out_of_range& e) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}
"."[0-9]+([eE][-+]?[0-9]+)? {
    // 对十进制浮点数的处理，小数点前没有数字的情况
    try {
        float fval = convertToFloatDec(yytext);
        return YaccParser::make_FLOAT_CONST(fval, loc);
    } catch (const std::out_of_range& e) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}
[0-9]+[eE][-+]?[0-9]+ {
    // 对十进制浮点数的处理，没有小数点只有指数部分的情况
    try {
        float fval = convertToFloatDec(yytext);
        return YaccParser::make_FLOAT_CONST(fval, loc);
    } catch (const std::out_of_range& e) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}

0[xX][0-9a-fA-F]+"."[0-9a-fA-F]*[pP][-+]?[0-9]+ {
    // 对十六进制浮点数的处理
    // 对于一般形式的处理
    try {
        float fval = convertToFloatHex(yytext);
        return YaccParser::make_FLOAT_CONST(fval, loc);
    } catch (...) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}
0[xX]"."[0-9a-fA-F]+[pP][-+]?[0-9]+ {
    // 对十六进制浮点数的处理，小数点前没有数字的情况
    try {
        float fval = convertToFloatHex(yytext);
        return YaccParser::make_FLOAT_CONST(fval, loc);
    } catch (...) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}
0[xX][0-9a-fA-F]+[pP][-+]?[0-9]+ {
    // 对十六进制浮点数的处理，没有小数点只有指数部分的情况
    try {
        float fval = convertToFloatHex(yytext);
        return YaccParser::make_FLOAT_CONST(fval, loc);
    } catch (...) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}

[A-Za-z_]+[A-Za-z0-9_]* {
    // 识别标识符，首字母或下划线，后续可以是字母、数字或下划线
    auto str = std::string(yytext);
    return YaccParser::make_IDENT(str, loc); 
}

\"[^\"]*\"          { 
    // 识别字符串常量，双引号包围，中间不能有双引号
    std::string str(yytext);
    if (!str.empty()) str = str.substr(1, str.size() - 2);
    return YaccParser::make_STR_CONST(str, loc); 
}
0[xX][0-9a-fA-F]+ {
    // 识别十六进制整数
    try {
        bool isLL = false;
        long long result = convertToInt(yytext, '\0', isLL);
        if (isLL) return YaccParser::make_LL_CONST(result, loc);
        return YaccParser::make_INT_CONST(static_cast<int>(result), loc);
    } catch (...) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}
0[0-7]+ {
    // 识别八进制整数
    try {
        bool isLL = false;
        long long result = convertToInt(yytext, '\0', isLL);
        if (isLL) return YaccParser::make_LL_CONST(result, loc);
        return YaccParser::make_INT_CONST(static_cast<int>(result), loc);
    } catch (...) {
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}
0 { // 识别数字0
    return YaccParser::make_INT_CONST(0, loc); }

[1-9][0-9]* {
    // 识别十进制整数
    try {
        bool isLL = false;
        // 调用 convertToInt 函数进行转换，对longlong的支持
        long long result = convertToInt(yytext, '\0', isLL);
        if (isLL) return YaccParser::make_LL_CONST(result, loc);
        return YaccParser::make_INT_CONST(static_cast<int>(result), loc);
    } catch (const std::exception& e) {
        _parser.reportError(loc, std::string("Error parsing hexadecimal int: ") + e.what());
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}

"("                 { RETT(LPAREN, loc) }
")"                 { RETT(RPAREN, loc) }
"["                 { RETT(LBRACKET, loc) }
"]"                 { RETT(RBRACKET, loc) }
"{"                 { RETT(LBRACE, loc) }
"}"                 { RETT(RBRACE, loc) }

<<EOF>>         { RETT(END, loc); }

%%

int handleTab()
{
    // 该函数用于处理制表符，根据当前位置计算制表符应占用的列数
    // 如，在TAB_WIDTH为4的情况下：
    // 对于" \t"，则会对 '\t' 返回 3
    // 对于"   \t"，则会对 '\t' 返回 1
    // 对于"    \t"，则会对 '\t' 返回 4

    int& c = loc.begin.column;
    // (c - 1) % TAB_WIDTH 计算当前列号相对于制表符宽度的偏移量。
    return TAB_WIDTH - ((c - 1) % TAB_WIDTH) - 1;
}

long long convertToInt(const char* str, const char end, bool& isLongLong)
{
    // 该函数用于将字符串转换为整数，能正确处理格式无误的十进制、十六进制和八进制整数
    // 接受一个 isLongLong 引用参数，用于指示结果是否超出 int 范围

    int         base       = 10; // 进制
    long long   result     = 0; // 结果
    int         isNegative = 0; // 是否为负数标志
    const char* ptr        = str;
    static int  zeroOffset = '0'; // 偏移量
    static int  aOffset    = 'a' - 10;  // 偏移量
    static int  AOffset    = 'A' - 10;  // 偏移量
    int*        offset     = NULL;  // 指向当前字符的偏移量
    isLongLong             = false; // 初始化为 false

    // 鄙人对此持保留意见
    if (*ptr == '-') // 处理正负号
    {
        isNegative = 1;
        ++ptr;
    }
    else if (*ptr == '+') { ++ptr; }

    if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X'))
    {
        // 处理十六进制前缀 "0x" 或 "0X"
        base = 16;
        ptr += 2;
    }
    else if (ptr[0] == '0')
    {
        // 8进制
        base = 8;
        ptr += 1;
    }

    while (*ptr != end)
    {
        // 处理每个字符
        int value = 0;

        // 为每个字符确定偏移量类型
        if (base == 16)
        {
            if (*ptr >= '0' && *ptr <= '9')
                offset = &zeroOffset;
            else if (*ptr >= 'a' && *ptr <= 'f')
                offset = &aOffset;
            else if (*ptr >= 'A' && *ptr <= 'F')
                offset = &AOffset;
        }
        else { offset = &zeroOffset; }

        // 求值
        value  = *ptr - *offset;
        result = result * base + value;
        ++ptr;
    }

    // 符号 
    if (isNegative) result = -result;

    // 检查是否超出 int 和 long long 范围
    if (result > std::numeric_limits<int>::max() || result < std::numeric_limits<int>::min())
    {
        isLongLong = true;
        if (result > std::numeric_limits<long long>::max() || result < std::numeric_limits<long long>::min())
        {
            throw std::out_of_range(str + std::string(" overflow or underflow for long long"));
        }
    }

    return result;
}

float convertToFloatDec(const char* str)
{
    // 该函数用于将字符串转换为浮点数，能正确处理格式无误的十进制浮点数
    const char* head = str;
    if (str == NULL) { return 0.0f; }

    // 处理正负号
    int sign = 1;
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+') { str++; }

    double integerPart = 0.0; // 整数部分
    while (isdigit(*str))
    { // 处理整数部分 isdigit:判断是否为数字字符
        integerPart = integerPart * 10 + (*str - '0');
        str++;
    }

    double fractionPart = 0.0; // 小数部分
    if (*str == '.')
    {
        str++;
        double divisor = 10.0; // 除数初始值为10.0
        while (isdigit(*str))
        {
            // 处理小数部分
            fractionPart += (*str - '0') / divisor;
            divisor *= 10.0;
            str++;
        }
    }
    // 计算总值
    double value = integerPart + fractionPart;

    // 处理指数部分
    if (*str == 'e' || *str == 'E')
    {
        str++;
        int expSign = 1;
        // 处理指数的正负号，不指明默认为正
        if (*str == '-')
        {
            expSign = -1;
            str++;
        }
        else if (*str == '+') { str++; }

        int exponent = 0; // 指数值
        while (isdigit(*str))
        {
            exponent = exponent * 10 + (*str - '0');
            str++;
        }
        // 计算指数部分的值
        value *= pow(10, expSign * exponent);
    }

    value = sign * value; // 应用正负号

    // 检查是否超出 float 范围
    if (value > std::numeric_limits<float>::max() || value < -std::numeric_limits<float>::max())
    {
        throw std::out_of_range(
            head + ((value > std::numeric_limits<float>::max()) ? std::string(" overflow") : std::string(" underflow")));
    }

    return static_cast<float>(value);
}

float convertToFloatHex(const char* str)
{
    // 该函数用于将字符串转换为浮点数，能正确处理格式无误的十六进制浮点数
    const char* head = str;
    if (str == NULL) { return 0.0f; }

    // 依旧处理正负号
    int sign = 1;
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+') { str++; }

    // 处理十六进制前缀 "0x" 或 "0X"
    if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X')) { str += 2; }

    // 处理整数部分
    unsigned long long integerPart = 0;
    while (isxdigit(*str))
    {
        int digit = 0;
        if (isdigit(*str)) { digit = (*str - '0'); }
        else if (*str >= 'a' && *str <= 'f') { digit = (*str - 'a' + 10); }
        else if (*str >= 'A' && *str <= 'F') { digit = (*str - 'A' + 10); }
        integerPart = integerPart * 16 + digit;
        str++;
    }

    double fractionPart = 0.0;
    if (*str == '.')
    {
        str++;
        unsigned long long numerator   = 0;
        unsigned long long denominator = 1;
        // 处理小数部分
        while (isxdigit(*str))
        {
            int digit = 0;
            if (isdigit(*str)) { digit = (*str - '0'); }
            else if (*str >= 'a' && *str <= 'f') { digit = (*str - 'a' + 10); }
            else if (*str >= 'A' && *str <= 'F') { digit = (*str - 'A' + 10); }
            numerator = numerator * 16 + digit;
            denominator *= 16;
            str++;
        }
        fractionPart = (double)numerator / (double)denominator;
    }

    double value = (double)integerPart + fractionPart; // 计算总值

    if (*str == 'p' || *str == 'P')
    { // 处理指数部分
        str++;
        // 处理指数的正负号，不指明默认为正
        int expSign = 1;
        if (*str == '-')
        {
            expSign = -1;
            str++;
        }
        else if (*str == '+') { str++; }

        int exponent = 0;
        while (isdigit(*str))
        {
            exponent = exponent * 10 + (*str - '0');
            str++;
        }
        // 计算指数部分的值
        value *= pow(2, expSign * exponent);
    }

    value = sign * value;

    if (value > std::numeric_limits<float>::max() || value < -std::numeric_limits<float>::max())
    {
        throw std::out_of_range(
            head + ((value > std::numeric_limits<float>::max()) ? std::string(" overflow") : std::string(" underflow")));
    }

    return static_cast<float>(value);
}
