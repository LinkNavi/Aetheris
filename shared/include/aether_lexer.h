#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace Aether {

    enum class TokenType : uint8_t {
        // Literals
        Number, String, Identifier,
        // Keywords
        KwSpell, KwRune, KwFn, KwReturn,
        KwIf, KwElse, KwFor, KwWhile, KwBreak, KwContinue,
        KwTrue, KwFalse, KwNull, KwLet,
        KwVec3, KwFail, KwLog,
        // Operators
        Plus, Minus, Star, Slash, Percent,
        Eq, EqEq, BangEq, Lt, Gt, LtEq, GtEq,
        And, Or, Bang,
        PlusEq, MinusEq, StarEq, SlashEq,
        // Punctuation
        LParen, RParen, LBrace, RBrace, LBracket, RBracket,
        Semicolon, Colon, Comma, Dot,
        // Special
        Eof, Error,
    };

    struct Token {
        TokenType   type;
        std::string value;
        int         line = 0;
    };

    class Lexer {
    public:
        explicit Lexer(std::string src);
        std::vector<Token> tokenize();

    private:
        std::string _src;
        int         _pos  = 0;
        int         _line = 1;

        char        peek(int offset = 0) const;
        char        advance();
        void        skipWhitespaceAndComments();
        Token       readNumber();
        Token       readString();
        Token       readIdentOrKeyword();
        Token       makeToken(TokenType t, std::string v = "") const;
    };

} // namespace Aether