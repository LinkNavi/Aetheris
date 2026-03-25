#include "aether_parser.h"
#include <stdexcept>

namespace Aether {

Parser::Parser(std::vector<Token> tokens) : _tokens(std::move(tokens)) {}

// ── Token helpers ─────────────────────────────────────────────────────────────

const Token& Parser::peek(int offset) const {
    int i = _pos + offset;
    if (i >= (int)_tokens.size()) return _tokens.back();
    return _tokens[i];
}

const Token& Parser::advance() {
    const Token& t = _tokens[_pos];
    if (_pos < (int)_tokens.size() - 1) _pos++;
    return t;
}

bool Parser::check(TokenType t) const { return peek().type == t; }

bool Parser::match(TokenType t) {
    if (check(t)) { advance(); return true; }
    return false;
}

bool Parser::expect(TokenType t, const char* msg) {
    if (check(t)) { advance(); return true; }
    setError(std::string(msg) + " (got '" + peek().value + "' on line " + std::to_string(peek().line) + ")");
    return false;
}

void Parser::setError(const std::string& msg) {
    if (_error.empty()) _error = msg;
}

ExprPtr Parser::makeExpr(ExprVariant v, int line) {
    auto e = std::make_unique<Expr>();
    e->node = std::move(v);
    e->line = line;
    return e;
}

StmtPtr Parser::makeStmt(StmtVariant v, int line) {
    auto s = std::make_unique<Stmt>();
    s->node = std::move(v);
    s->line = line;
    return s;
}

// ── Top level ─────────────────────────────────────────────────────────────────

Program Parser::parse() {
    Program prog;
    while (!check(TokenType::Eof) && _error.empty()) {
        prog.stmts.push_back(parseStmt());
    }
    return prog;
}

// ── Statements ────────────────────────────────────────────────────────────────

StmtPtr Parser::parseStmt() {
    int line = peek().line;
    if (check(TokenType::KwLet))    return parseLet();
    if (check(TokenType::KwIf))     return parseIf();
    if (check(TokenType::KwWhile))  return parseWhile();
    if (check(TokenType::KwFor))    return parseFor();
    if (check(TokenType::KwReturn)) return parseReturn();
    if (check(TokenType::KwFn))     return parseFnDecl();
    if (check(TokenType::KwSpell))  return parseSpellDecl();
    if (check(TokenType::LBrace))   return parseBlock();
    if (match(TokenType::KwBreak))    { match(TokenType::Semicolon); return makeStmt(BreakStmt{}, line); }
    if (match(TokenType::KwContinue)) { match(TokenType::Semicolon); return makeStmt(ContinueStmt{}, line); }
    return parseExprStmt();
}

StmtPtr Parser::parseBlock() {
    int line = peek().line;
    expect(TokenType::LBrace, "Expected '{'");
    BlockStmt block;
    while (!check(TokenType::RBrace) && !check(TokenType::Eof) && _error.empty())
        block.body.push_back(parseStmt());
    expect(TokenType::RBrace, "Expected '}'");
    return makeStmt(std::move(block), line);
}

StmtPtr Parser::parseLet() {
    int line = peek().line;
    advance(); // let
    std::string name = peek().value;
    expect(TokenType::Identifier, "Expected variable name");
    LetStmt s;
    s.name = name;
    if (match(TokenType::Eq)) s.init = parseExpr();
    match(TokenType::Semicolon);
    return makeStmt(std::move(s), line);
}

StmtPtr Parser::parseIf() {
    int line = peek().line;
    advance(); // if
    expect(TokenType::LParen, "Expected '(' after if");
    auto cond = parseExpr();
    expect(TokenType::RParen, "Expected ')' after condition");
    auto then = parseStmt();
    IfStmt s;
    s.condition  = std::move(cond);
    s.thenBranch = std::move(then);
    if (match(TokenType::KwElse)) s.elseBranch = parseStmt();
    return makeStmt(std::move(s), line);
}

StmtPtr Parser::parseWhile() {
    int line = peek().line;
    advance(); // while
    expect(TokenType::LParen, "Expected '(' after while");
    auto cond = parseExpr();
    expect(TokenType::RParen, "Expected ')'");
    auto body = parseStmt();
    WhileStmt s;
    s.condition = std::move(cond);
    s.body      = std::move(body);
    return makeStmt(std::move(s), line);
}

StmtPtr Parser::parseFor() {
    // for (i = 0; i < N; i++) { ... }
    // simplified: for (let i = start; i < end; i++)
    int line = peek().line;
    advance(); // for
    expect(TokenType::LParen, "Expected '('");

    // init: let i = expr or i = expr
    std::string varName;
    if (match(TokenType::KwLet)) {
        varName = peek().value;
        expect(TokenType::Identifier, "Expected loop var name");
    } else {
        varName = peek().value;
        expect(TokenType::Identifier, "Expected loop var name");
    }
    expect(TokenType::Eq, "Expected '=' in for init");
    auto fromExpr = parseExpr();
    expect(TokenType::Semicolon, "Expected ';'");

    // condition: i < expr  (we extract the RHS as the 'to' value)
    // parse the full condition expression and store it properly
    // For simplicity: read identifier, comparator, and to-expr
    std::string condVar = peek().value;
    expect(TokenType::Identifier, "Expected loop var in condition");
    // skip the comparator token (we assume < for now, store full cond)
    advance(); // < or <=
    auto toExpr = parseExpr();
    expect(TokenType::Semicolon, "Expected ';'");

    // increment: skip (i++, i+=1, i = i + 1 — we auto-increment by 1)
    while (!check(TokenType::RParen) && !check(TokenType::Eof)) advance();
    expect(TokenType::RParen, "Expected ')'");

    auto body = parseStmt();
    ForStmt s;
    s.init = varName;
    s.from = std::move(fromExpr);
    s.to   = std::move(toExpr);
    s.body = std::move(body);
    return makeStmt(std::move(s), line);
}

StmtPtr Parser::parseReturn() {
    int line = peek().line;
    advance(); // return
    ReturnStmt s;
    if (!check(TokenType::Semicolon) && !check(TokenType::RBrace))
        s.value = parseExpr();
    match(TokenType::Semicolon);
    return makeStmt(std::move(s), line);
}

StmtPtr Parser::parseFnDecl() {
    int line = peek().line;
    advance(); // fn
    std::string name = peek().value;
    expect(TokenType::Identifier, "Expected function name");
    expect(TokenType::LParen, "Expected '('");
    FnDecl decl;
    decl.name = name;
    while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
        decl.params.push_back(peek().value);
        expect(TokenType::Identifier, "Expected param name");
        match(TokenType::Comma);
    }
    expect(TokenType::RParen, "Expected ')'");
    decl.body = parseBlock();
    return makeStmt(std::move(decl), line);
}

StmtPtr Parser::parseSpellDecl() {
    int line = peek().line;
    advance(); // spell
    std::string name = peek().value;
    expect(TokenType::Identifier, "Expected spell name");
    expect(TokenType::LBrace, "Expected '{'");
    // look for cast(params) { body }
    SpellDecl decl;
    decl.name = name;
    // optional: parse cast(...) { ... } inside the braces
    if (check(TokenType::Identifier) && peek().value == "cast") {
        advance();
        expect(TokenType::LParen, "Expected '('");
        while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
            decl.params.push_back(peek().value);
            expect(TokenType::Identifier, "Expected param name");
            match(TokenType::Comma);
        }
        expect(TokenType::RParen, "Expected ')'");
        decl.body = parseBlock();
    }
    expect(TokenType::RBrace, "Expected '}'");
    return makeStmt(std::move(decl), line);
}

StmtPtr Parser::parseExprStmt() {
    int line = peek().line;
    auto expr = parseExpr();
    match(TokenType::Semicolon);
    return makeStmt(ExprStmt{std::move(expr)}, line);
}

// ── Expressions (precedence climbing) ────────────────────────────────────────

ExprPtr Parser::parseExpr()       { return parseAssign(); }

ExprPtr Parser::parseAssign() {
    int line = peek().line;
    // peek ahead: ident followed by = / += / -= etc?
    if (check(TokenType::Identifier)) {
        int save = _pos;
        std::string name = advance().value;
        std::string op;
        if      (match(TokenType::Eq))      op = "=";
        else if (match(TokenType::PlusEq))  op = "+=";
        else if (match(TokenType::MinusEq)) op = "-=";
        else if (match(TokenType::StarEq))  op = "*=";
        else if (match(TokenType::SlashEq)) op = "/=";

        if (!op.empty()) {
            auto val = parseExpr();
            AssignExpr a;
            a.name  = name;
            a.op    = op;
            a.value = std::move(val);
            return makeExpr(std::move(a), line);
        }
        _pos = save; // backtrack
    }
    return parseOr();
}

ExprPtr Parser::parseOr() {
    int line = peek().line;
    auto left = parseAnd();
    while (match(TokenType::Or)) {
        auto right = parseAnd();
        BinopExpr b; b.op = "||"; b.left = std::move(left); b.right = std::move(right);
        left = makeExpr(std::move(b), line);
    }
    return left;
}

ExprPtr Parser::parseAnd() {
    int line = peek().line;
    auto left = parseEquality();
    while (match(TokenType::And)) {
        auto right = parseEquality();
        BinopExpr b; b.op = "&&"; b.left = std::move(left); b.right = std::move(right);
        left = makeExpr(std::move(b), line);
    }
    return left;
}

ExprPtr Parser::parseEquality() {
    int line = peek().line;
    auto left = parseComparison();
    while (check(TokenType::EqEq) || check(TokenType::BangEq)) {
        std::string op = advance().value;
        auto right = parseComparison();
        BinopExpr b; b.op = op; b.left = std::move(left); b.right = std::move(right);
        left = makeExpr(std::move(b), line);
    }
    return left;
}

ExprPtr Parser::parseComparison() {
    int line = peek().line;
    auto left = parseAddSub();
    while (check(TokenType::Lt) || check(TokenType::Gt) ||
           check(TokenType::LtEq) || check(TokenType::GtEq)) {
        std::string op = advance().value;
        auto right = parseAddSub();
        BinopExpr b; b.op = op; b.left = std::move(left); b.right = std::move(right);
        left = makeExpr(std::move(b), line);
    }
    return left;
}

ExprPtr Parser::parseAddSub() {
    int line = peek().line;
    auto left = parseMulDiv();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        std::string op = advance().value;
        auto right = parseMulDiv();
        BinopExpr b; b.op = op; b.left = std::move(left); b.right = std::move(right);
        left = makeExpr(std::move(b), line);
    }
    return left;
}

ExprPtr Parser::parseMulDiv() {
    int line = peek().line;
    auto left = parseUnary();
    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        std::string op = advance().value;
        auto right = parseUnary();
        BinopExpr b; b.op = op; b.left = std::move(left); b.right = std::move(right);
        left = makeExpr(std::move(b), line);
    }
    return left;
}

ExprPtr Parser::parseUnary() {
    int line = peek().line;
    if (check(TokenType::Bang) || check(TokenType::Minus)) {
        std::string op = advance().value;
        auto operand = parseUnary();
        UnaryExpr u; u.op = op; u.operand = std::move(operand);
        return makeExpr(std::move(u), line);
    }
    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    int line = peek().line;
    auto expr = parsePrimary();
    while (true) {
        if (check(TokenType::LParen)) {
            expr = parseCall(std::move(expr), line);
        } else if (match(TokenType::Dot)) {
            std::string member = peek().value;
            expect(TokenType::Identifier, "Expected member name");
            MemberExpr m; m.object = std::move(expr); m.member = member;
            expr = makeExpr(std::move(m), line);
        } else if (match(TokenType::LBracket)) {
            auto idx = parseExpr();
            expect(TokenType::RBracket, "Expected ']'");
            IndexExpr ix; ix.object = std::move(expr); ix.index = std::move(idx);
            expr = makeExpr(std::move(ix), line);
        } else break;
    }
    return expr;
}

ExprPtr Parser::parseCall(ExprPtr callee, int line) {
    // callee must be an ident at this point
    std::string name;
    if (auto* id = std::get_if<IdentExpr>(&callee->node)) name = id->name;

    advance(); // (
    CallExpr call;
    call.callee = name;

    while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
        // named arg: ident: expr
        if (check(TokenType::Identifier) && peek(1).type == TokenType::Colon) {
            std::string argName = advance().value;
            advance(); // :
            auto val = parseExpr();
            call.namedArgs.push_back({argName, std::move(val)});
        } else {
            call.args.push_back(parseExpr());
        }
        match(TokenType::Comma);
    }
    expect(TokenType::RParen, "Expected ')'");
    return makeExpr(std::move(call), line);
}

ExprPtr Parser::parsePrimary() {
    int line = peek().line;
    auto& t = peek();

    if (t.type == TokenType::Number) {
        double v = std::stod(advance().value);
        return makeExpr(NumberExpr{v}, line);
    }
    if (t.type == TokenType::String) {
        std::string v = advance().value;
        return makeExpr(StringExpr{std::move(v)}, line);
    }
    if (t.type == TokenType::KwTrue)  { advance(); return makeExpr(BoolExpr{true},  line); }
    if (t.type == TokenType::KwFalse) { advance(); return makeExpr(BoolExpr{false}, line); }
    if (t.type == TokenType::KwNull)  { advance(); return makeExpr(NullExpr{},      line); }

    if (t.type == TokenType::KwVec3) {
        advance();
        expect(TokenType::LParen, "Expected '(' after vec3");
        auto x = parseExpr(); match(TokenType::Comma);
        auto y = parseExpr(); match(TokenType::Comma);
        auto z = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
        Vec3Expr v; v.x = std::move(x); v.y = std::move(y); v.z = std::move(z);
        return makeExpr(std::move(v), line);
    }

    if (t.type == TokenType::KwFail) {
        advance();
        expect(TokenType::LParen, "Expected '('");
        auto msg = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
        CallExpr c; c.callee = "__fail__"; c.args.push_back(std::move(msg));
        return makeExpr(std::move(c), line);
    }

    if (t.type == TokenType::KwLog) {
        advance();
        expect(TokenType::LParen, "Expected '('");
        auto msg = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
        CallExpr c; c.callee = "__log__"; c.args.push_back(std::move(msg));
        return makeExpr(std::move(c), line);
    }

    if (t.type == TokenType::Identifier) {
        std::string name = advance().value;
        return makeExpr(IdentExpr{name}, line);
    }

    if (t.type == TokenType::LParen) {
        advance();
        auto e = parseExpr();
        expect(TokenType::RParen, "Expected ')'");
        return e;
    }

    setError("Unexpected token '" + t.value + "' on line " + std::to_string(line));
    advance();
    return makeExpr(NullExpr{}, line);
}

} // namespace Aether