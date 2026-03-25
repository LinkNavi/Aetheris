#pragma once
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <string>
#include <vector>
namespace Aether {

// ── Forward declarations ──────────────────────────────────────────────────────
struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ── Expression node types ─────────────────────────────────────────────────────
struct NumberExpr   { double value; };
struct StringExpr   { std::string value; };
struct BoolExpr     { bool value; };
struct NullExpr     {};
struct IdentExpr    { std::string name; };

struct BinopExpr {
    std::string op;
    ExprPtr     left, right;
};

struct UnaryExpr {
    std::string op;
    ExprPtr     operand;
};

struct AssignExpr {
    std::string name;
    std::string op; // =, +=, -=, *=, /=
    ExprPtr     value;
};

struct CallExpr {
    std::string            callee;
    std::vector<ExprPtr>   args;
    // Named params: e.g. projectile(..., speed: 22.0)
    std::vector<std::pair<std::string, ExprPtr>> namedArgs;
};

struct IndexExpr {
    ExprPtr                object;
    ExprPtr                index;
};

struct MemberExpr {
    ExprPtr     object;
    std::string member;
};

struct Vec3Expr {
    ExprPtr x, y, z;
};

// ── Expr variant ──────────────────────────────────────────────────────────────
#include <variant>
using ExprVariant = std::variant<
    NumberExpr, StringExpr, BoolExpr, NullExpr,
    IdentExpr, BinopExpr, UnaryExpr, AssignExpr,
    CallExpr, IndexExpr, MemberExpr, Vec3Expr
>;

struct Expr {
    ExprVariant node;
    int         line = 0;
};

// ── Statement node types ──────────────────────────────────────────────────────
struct ExprStmt     { ExprPtr expr; };

struct LetStmt {
    std::string name;
    ExprPtr     init; // may be null
};

struct ReturnStmt   { ExprPtr value; }; // value may be null

struct BreakStmt    {};
struct ContinueStmt {};

struct BlockStmt    { std::vector<StmtPtr> body; };

struct IfStmt {
    ExprPtr              condition;
    StmtPtr              thenBranch;
    StmtPtr              elseBranch; // may be null
};

struct WhileStmt {
    ExprPtr condition;
    StmtPtr body;
};

struct ForStmt {
    std::string init;   // variable name
    ExprPtr     from;
    ExprPtr     to;
    StmtPtr     body;
};

struct FnDecl {
    std::string            name;
    std::vector<std::string> params;
    StmtPtr                body;
};

struct SpellDecl {
    std::string            name;
    std::vector<std::string> params;
    StmtPtr                body;
};

// ── Stmt variant ──────────────────────────────────────────────────────────────
using StmtVariant = std::variant<
    ExprStmt, LetStmt, ReturnStmt, BreakStmt, ContinueStmt,
    BlockStmt, IfStmt, WhileStmt, ForStmt,
    FnDecl, SpellDecl
>;

struct Stmt {
    StmtVariant node;
    int         line = 0;
};

// ── Top-level program ─────────────────────────────────────────────────────────
struct Program {
    std::vector<StmtPtr> stmts;
};

} // namespace Aether