#pragma once
#include "aether_ast.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <memory>
#include <utility>
#include <stdexcept>
namespace Aether {

// ── Runtime value ─────────────────────────────────────────────────────────────
struct Vec3Val { float x, y, z; };

struct AetherArray {
    std::vector<struct Value> elems;
};

struct Value {
    using Var = std::variant<
        std::monostate,   // null
        bool,
        double,
        std::string,
        Vec3Val,
        std::shared_ptr<AetherArray>
    >;
    Var data;

    static Value null()                { return {std::monostate{}}; }
    static Value boolean(bool b)       { return {b}; }
    static Value number(double d)      { return {d}; }
    static Value string(std::string s) { return {std::move(s)}; }
    static Value vec3(float x, float y, float z) { return {Vec3Val{x,y,z}}; }
    static Value array()               { return {std::make_shared<AetherArray>()}; }

    bool isNull()   const { return std::holds_alternative<std::monostate>(data); }
    bool isBool()   const { return std::holds_alternative<bool>(data); }
    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isVec3()   const { return std::holds_alternative<Vec3Val>(data); }
    bool isArray()  const { return std::holds_alternative<std::shared_ptr<AetherArray>>(data); }

    bool   asBool()   const { return std::get<bool>(data); }
    double asNumber() const { return std::get<double>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    Vec3Val asVec3()  const { return std::get<Vec3Val>(data); }
    std::shared_ptr<AetherArray> asArray() const { return std::get<std::shared_ptr<AetherArray>>(data); }

    bool truthy() const {
        if (isNull())   return false;
        if (isBool())   return asBool();
        if (isNumber()) return asNumber() != 0.0;
        if (isString()) return !asString().empty();
        return true;
    }

    std::string toString() const;
};

// ── Native function signature ─────────────────────────────────────────────────
using NativeArgs      = std::vector<Value>;
using NativeNamedArgs = std::unordered_map<std::string, Value>;
using NativeFn        = std::function<Value(NativeArgs, NativeNamedArgs)>;

// ── Environment (scope chain) ─────────────────────────────────────────────────
struct Env {
    std::unordered_map<std::string, Value> vars;
    std::shared_ptr<Env>                   parent;

    explicit Env(std::shared_ptr<Env> p = nullptr) : parent(std::move(p)) {}

    void set(const std::string& name, Value v) { vars[name] = std::move(v); }

    bool assign(const std::string& name, Value v) {
        auto it = vars.find(name);
        if (it != vars.end()) { it->second = std::move(v); return true; }
        if (parent) return parent->assign(name, std::move(v));
        return false;
    }

    Value* lookup(const std::string& name) {
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        if (parent) return parent->lookup(name);
        return nullptr;
    }
};

// ── Script-defined function ───────────────────────────────────────────────────
struct ScriptFn {
    std::string              name;
    std::vector<std::string> params;
    Stmt*                    body = nullptr; // non-owning ptr into Program
    std::shared_ptr<Env>     closure;
};

// ── VM execution result ───────────────────────────────────────────────────────
struct ExecResult {
    bool        ok     = true;
    std::string error;
    Value       retval;

    static ExecResult success(Value v = Value::null()) { return {true, {}, std::move(v)}; }
    static ExecResult fail(std::string e)              { return {false, std::move(e), Value::null()}; }
};

// ── Control flow signals ──────────────────────────────────────────────────────
struct ReturnSignal   { Value value; };
struct BreakSignal    {};
struct ContinueSignal {};
struct FailSignal     { std::string reason; };

// ── Interpreter ──────────────────────────────────────────────────────────────
class Interpreter {
public:
    static constexpr int MAX_INSTRUCTIONS = 512;
    static constexpr int MAX_LOOP_ITERS   = 8;
    static constexpr int MAX_CALL_DEPTH   = 32;

    Interpreter();

    // Register a C++ function callable from scripts
    void registerNative(const std::string& name, NativeFn fn);

    // Load a compiled program — call once per script
    void loadProgram(Program* prog);

    // Call a named spell/fn with positional args
    ExecResult call(const std::string& name, std::vector<Value> args);

    // Convenience: parse + eval a one-liner (testing)
    ExecResult evalSnippet(const std::string& src);

    bool hasFn(const std::string& name) const;

private:
    Program*                                  _prog = nullptr;
    std::unordered_map<std::string, NativeFn> _natives;
    std::unordered_map<std::string, ScriptFn> _fns;
    int                                       _instrCount = 0;
    int                                       _callDepth  = 0;

    void  loadDecls(std::shared_ptr<Env> env);

    Value evalExpr (const Expr& e,  std::shared_ptr<Env> env);
    void  execStmt (const Stmt& s,  std::shared_ptr<Env> env);
    void  execBlock(const std::vector<StmtPtr>& body, std::shared_ptr<Env> env);

    Value callScriptFn(const ScriptFn& fn, std::vector<Value> args);
    Value callNative  (const std::string& name, NativeArgs args, NativeNamedArgs named);

    Value evalBinop (const BinopExpr&  e, std::shared_ptr<Env> env);
    Value evalCall  (const CallExpr&   e, std::shared_ptr<Env> env);
    Value evalAssign(const AssignExpr& e, std::shared_ptr<Env> env);

    void tick(); // count instruction, throw on limit
};

} // namespace Aether