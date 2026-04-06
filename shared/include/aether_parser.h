#pragma once
#include "aether_lexer.h"
#include "aether_ast.h"
#include <string>

namespace Aether {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Program parse();

    const std::string& error() const { return _error; }
    bool               hasError() const { return !_error.empty(); }

private:
    std::vector<Token> _tokens;
    int                _pos   = 0;
    std::string        _error;

    // ── Token helpers ─────────────────────────────────────────────────────
    const Token& peek(int offset = 0) const;
    const Token& advance();
    bool         check(TokenType t) const;
    bool         match(TokenType t);
    bool         expect(TokenType t, const char* msg);

    // ── Statements ────────────────────────────────────────────────────────
    StmtPtr parseStmt();
    StmtPtr parseBlock();
    StmtPtr parseLet();
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseFor();
    StmtPtr parseReturn();
    StmtPtr parseFnDecl();
    StmtPtr parseSpellDecl();
    StmtPtr parseRuneDecl();
    StmtPtr parseExprStmt();

    // ── Expressions ───────────────────────────────────────────────────────
    ExprPtr parseExpr();
    ExprPtr parseAssign();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseEquality();
    ExprPtr parseComparison();
    ExprPtr parseAddSub();
    ExprPtr parseMulDiv();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    ExprPtr parseCall(ExprPtr callee, int line);

    ExprPtr makeExpr(ExprVariant v, int line);
    StmtPtr makeStmt(StmtVariant v, int line);

    void setError(const std::string& msg);
};

} // namespace Aether
