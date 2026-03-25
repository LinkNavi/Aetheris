#include "aether_lexer.h"
#include <cctype>
#include <unordered_map>

namespace Aether {

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"spell",    TokenType::KwSpell},
    {"rune",     TokenType::KwRune},
    {"fn",       TokenType::KwFn},
    {"return",   TokenType::KwReturn},
    {"if",       TokenType::KwIf},
    {"else",     TokenType::KwElse},
    {"for",      TokenType::KwFor},
    {"while",    TokenType::KwWhile},
    {"break",    TokenType::KwBreak},
    {"continue", TokenType::KwContinue},
    {"true",     TokenType::KwTrue},
    {"false",    TokenType::KwFalse},
    {"null",     TokenType::KwNull},
    {"let",      TokenType::KwLet},
    {"vec3",     TokenType::KwVec3},
    {"fail",     TokenType::KwFail},
    {"log",      TokenType::KwLog},
};

Lexer::Lexer(std::string src) : _src(std::move(src)) {}

char Lexer::peek(int offset) const {
    int i = _pos + offset;
    return (i < (int)_src.size()) ? _src[i] : '\0';
}

char Lexer::advance() {
    char c = _src[_pos++];
    if (c == '\n') _line++;
    return c;
}

Token Lexer::makeToken(TokenType t, std::string v) const {
    return {t, std::move(v), _line};
}

void Lexer::skipWhitespaceAndComments() {
    while (_pos < (int)_src.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (_pos < (int)_src.size() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            advance(); advance();
            while (_pos < (int)_src.size()) {
                if (peek() == '*' && peek(1) == '/') { advance(); advance(); break; }
                advance();
            }
        } else break;
    }
}

Token Lexer::readNumber() {
    int start = _pos;
    while (std::isdigit(peek())) advance();
    if (peek() == '.' && std::isdigit(peek(1))) {
        advance();
        while (std::isdigit(peek())) advance();
    }
    return makeToken(TokenType::Number, _src.substr(start, _pos - start));
}

Token Lexer::readString() {
    advance(); // opening "
    std::string val;
    while (_pos < (int)_src.size() && peek() != '"') {
        char c = advance();
        if (c == '\\') {
            char e = advance();
            switch (e) {
            case 'n': val += '\n'; break;
            case 't': val += '\t'; break;
            default:  val += e;   break;
            }
        } else val += c;
    }
    if (peek() == '"') advance(); // closing "
    return makeToken(TokenType::String, val);
}

Token Lexer::readIdentOrKeyword() {
    int start = _pos;
    while (std::isalnum(peek()) || peek() == '_') advance();
    std::string word = _src.substr(start, _pos - start);
    auto it = KEYWORDS.find(word);
    if (it != KEYWORDS.end()) return makeToken(it->second, word);
    return makeToken(TokenType::Identifier, word);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        skipWhitespaceAndComments();
        if (_pos >= (int)_src.size()) { tokens.push_back(makeToken(TokenType::Eof)); break; }

        char c = peek();

        if (std::isdigit(c))           { tokens.push_back(readNumber()); continue; }
        if (c == '"')                  { tokens.push_back(readString()); continue; }
        if (std::isalpha(c) || c=='_') { tokens.push_back(readIdentOrKeyword()); continue; }

        advance();
        switch (c) {
        case '+': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::PlusEq)  : TokenType::Plus));  break;
        case '-': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::MinusEq) : TokenType::Minus)); break;
        case '*': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::StarEq)  : TokenType::Star));  break;
        case '/': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::SlashEq) : TokenType::Slash)); break;
        case '%': tokens.push_back(makeToken(TokenType::Percent)); break;
        case '=': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::EqEq)   : TokenType::Eq));    break;
        case '!': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::BangEq) : TokenType::Bang));  break;
        case '<': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::LtEq)   : TokenType::Lt));    break;
        case '>': tokens.push_back(makeToken(peek()=='=' ? (advance(),TokenType::GtEq)   : TokenType::Gt));    break;
        case '&': if (peek()=='&') { advance(); tokens.push_back(makeToken(TokenType::And)); } break;
        case '|': if (peek()=='|') { advance(); tokens.push_back(makeToken(TokenType::Or));  } break;
        case '(': tokens.push_back(makeToken(TokenType::LParen));   break;
        case ')': tokens.push_back(makeToken(TokenType::RParen));   break;
        case '{': tokens.push_back(makeToken(TokenType::LBrace));   break;
        case '}': tokens.push_back(makeToken(TokenType::RBrace));   break;
        case '[': tokens.push_back(makeToken(TokenType::LBracket)); break;
        case ']': tokens.push_back(makeToken(TokenType::RBracket)); break;
        case ';': tokens.push_back(makeToken(TokenType::Semicolon)); break;
        case ':': tokens.push_back(makeToken(TokenType::Colon));    break;
        case ',': tokens.push_back(makeToken(TokenType::Comma));    break;
        case '.': tokens.push_back(makeToken(TokenType::Dot));      break;
        default:  tokens.push_back(makeToken(TokenType::Error, std::string(1,c))); break;
        }
    }
    return tokens;
}

} // namespace Aether