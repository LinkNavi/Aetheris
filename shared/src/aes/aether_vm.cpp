#include "aether_vm.h"
#include "aether_lexer.h"
#include "aether_parser.h"
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace Aether {

// ── Value::toString ───────────────────────────────────────────────────────────

std::string Value::toString() const {
    if (isNull())   return "null";
    if (isBool())   return asBool() ? "true" : "false";
    if (isNumber()) {
        double d = asNumber();
        if (d == std::floor(d)) return std::to_string((long long)d);
        std::ostringstream ss; ss << d; return ss.str();
    }
    if (isString()) return asString();
    if (isVec3()) {
        auto v = asVec3();
        std::ostringstream ss;
        ss << "vec3(" << v.x << "," << v.y << "," << v.z << ")";
        return ss.str();
    }
    if (isArray()) return "[array]";
    return "?";
}

// ── Interpreter ctor — register builtins ──────────────────────────────────────

Interpreter::Interpreter() {
    // Math
    registerNative("sqrt",  [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.empty() || !a[0].isNumber()) return Value::null();
        return Value::number(std::sqrt(a[0].asNumber()));
    });
    registerNative("abs",   [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.empty() || !a[0].isNumber()) return Value::null();
        return Value::number(std::abs(a[0].asNumber()));
    });
    registerNative("floor", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.empty() || !a[0].isNumber()) return Value::null();
        return Value::number(std::floor(a[0].asNumber()));
    });
    registerNative("ceil",  [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.empty() || !a[0].isNumber()) return Value::null();
        return Value::number(std::ceil(a[0].asNumber()));
    });
    registerNative("min",   [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.size()<2 || !a[0].isNumber() || !a[1].isNumber()) return Value::null();
        return Value::number(std::min(a[0].asNumber(), a[1].asNumber()));
    });
    registerNative("max",   [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.size()<2 || !a[0].isNumber() || !a[1].isNumber()) return Value::null();
        return Value::number(std::max(a[0].asNumber(), a[1].asNumber()));
    });
    registerNative("clamp", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.size()<3) return Value::null();
        double v = a[0].asNumber(), lo = a[1].asNumber(), hi = a[2].asNumber();
        return Value::number(std::max(lo, std::min(hi, v)));
    });
    registerNative("lerp",  [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.size()<3) return Value::null();
        if (a[0].isNumber()) {
            double t = a[2].asNumber();
            return Value::number(a[0].asNumber() + (a[1].asNumber()-a[0].asNumber())*t);
        }
        if (a[0].isVec3() && a[1].isVec3()) {
            auto v0 = a[0].asVec3(), v1 = a[1].asVec3();
            float t = (float)a[2].asNumber();
            return Value::vec3(v0.x+(v1.x-v0.x)*t, v0.y+(v1.y-v0.y)*t, v0.z+(v1.z-v0.z)*t);
        }
        return Value::null();
    });

    // Vec3 operations
    registerNative("normalize", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.empty() || !a[0].isVec3()) return Value::null();
        auto v = a[0].asVec3();
        float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        if (len < 1e-7f) return Value::vec3(0,0,0);
        return Value::vec3(v.x/len, v.y/len, v.z/len);
    });
    registerNative("length", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.empty() || !a[0].isVec3()) return Value::null();
        auto v = a[0].asVec3();
        return Value::number(std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z));
    });
    registerNative("distance", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.size()<2 || !a[0].isVec3() || !a[1].isVec3()) return Value::null();
        auto v0=a[0].asVec3(), v1=a[1].asVec3();
        float dx=v0.x-v1.x, dy=v0.y-v1.y, dz=v0.z-v1.z;
        return Value::number(std::sqrt(dx*dx+dy*dy+dz*dz));
    });
    registerNative("dot", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.size()<2 || !a[0].isVec3() || !a[1].isVec3()) return Value::null();
        auto v0=a[0].asVec3(), v1=a[1].asVec3();
        return Value::number(v0.x*v1.x + v0.y*v1.y + v0.z*v1.z);
    });

    // Array
    registerNative("length", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (!a.empty() && a[0].isArray()) return Value::number((double)a[0].asArray()->elems.size());
        return Value::number(0);
    });
    registerNative("push", [](NativeArgs a, NativeNamedArgs) -> Value {
        if (a.size()<2 || !a[0].isArray()) return Value::null();
        a[0].asArray()->elems.push_back(a[1]);
        return Value::null();
    });
    registerNative("array", [](NativeArgs, NativeNamedArgs) -> Value {
        return Value::array();
    });

    // Type checks
    registerNative("is_null",   [](NativeArgs a, NativeNamedArgs) -> Value { return Value::boolean(!a.empty() && a[0].isNull()); });
    registerNative("is_number", [](NativeArgs a, NativeNamedArgs) -> Value { return Value::boolean(!a.empty() && a[0].isNumber()); });
    registerNative("to_string", [](NativeArgs a, NativeNamedArgs) -> Value {
        return Value::string(a.empty() ? "null" : a[0].toString());
    });

    // __fail__ and __log__ (wired up by keyword parsing)
    registerNative("__fail__", [](NativeArgs a, NativeNamedArgs) -> Value {
        throw FailSignal{a.empty() ? "fail" : a[0].toString()};
        return Value::null();
    });
    registerNative("__log__", [](NativeArgs a, NativeNamedArgs) -> Value {
        // Default: no-op. Game registers its own version.
        (void)a; return Value::null();
    });
}

void Interpreter::registerNative(const std::string& name, NativeFn fn) {
    _natives[name] = std::move(fn);
}

void Interpreter::loadProgram(Program* prog) {
    _prog = prog;
    _fns.clear();
    auto env = std::make_shared<Env>();
    loadDecls(env);
}

bool Interpreter::hasFn(const std::string& name) const {
    return _fns.count(name) > 0 || _natives.count(name) > 0;
}

void Interpreter::loadDecls(std::shared_ptr<Env> env) {
    if (!_prog) return;
    for (auto& s : _prog->stmts) {
        if (auto* fn = std::get_if<FnDecl>(&s->node)) {
            ScriptFn sf;
            sf.name    = fn->name;
            sf.params  = fn->params;
            sf.body    = fn->body.get();
            sf.closure = env;
            _fns[fn->name] = std::move(sf);
        }
        if (auto* sp = std::get_if<SpellDecl>(&s->node)) {
            ScriptFn sf;
            sf.name    = sp->name;
            sf.params  = sp->params;
            sf.body    = sp->body.get();
            sf.closure = env;
            _fns[sp->name] = std::move(sf);
        }
    }
}

// ── Public call entry point ───────────────────────────────────────────────────

ExecResult Interpreter::call(const std::string& name, std::vector<Value> args) {
    _instrCount = 0;
    _callDepth  = 0;
    try {
        auto it = _fns.find(name);
        if (it == _fns.end()) return ExecResult::fail("Unknown function: " + name);
        Value ret = callScriptFn(it->second, std::move(args));
        return ExecResult::success(std::move(ret));
    } catch (FailSignal& f) {
        return ExecResult::fail("Script fail: " + f.reason);
    } catch (std::runtime_error& e) {
        return ExecResult::fail(e.what());
    }
}

ExecResult Interpreter::evalSnippet(const std::string& src) {
    Lexer lex(src);
    auto tokens = lex.tokenize();
    Parser p(std::move(tokens));
    auto prog = p.parse();
    if (p.hasError()) return ExecResult::fail(p.error());

    _instrCount = 0;
    _callDepth  = 0;
    auto env = std::make_shared<Env>();
    // load any decls in snippet
    Program* saved = _prog;
    _prog = &prog;
    loadDecls(env);
    _prog = saved;

    try {
        Value last = Value::null();
        for (auto& s : prog.stmts) {
            if (auto* es = std::get_if<ExprStmt>(&s->node))
                last = evalExpr(*es->expr, env);
            else
                execStmt(*s, env);
        }
        return ExecResult::success(last);
    } catch (ReturnSignal& r) {
        return ExecResult::success(r.value);
    } catch (FailSignal& f) {
        return ExecResult::fail(f.reason);
    } catch (std::runtime_error& e) {
        return ExecResult::fail(e.what());
    }
}

// ── tick ─────────────────────────────────────────────────────────────────────

void Interpreter::tick() {
    if (++_instrCount > MAX_INSTRUCTIONS)
        throw std::runtime_error("Instruction limit exceeded");
}

// ── callScriptFn ─────────────────────────────────────────────────────────────

Value Interpreter::callScriptFn(const ScriptFn& fn, std::vector<Value> args) {
    if (++_callDepth > MAX_CALL_DEPTH)
        throw std::runtime_error("Call stack overflow");

    auto env = std::make_shared<Env>(fn.closure);
    for (int i = 0; i < (int)fn.params.size(); i++)
        env->set(fn.params[i], i < (int)args.size() ? args[i] : Value::null());

    Value ret = Value::null();
    try {
        if (fn.body) execStmt(*fn.body, env);
    } catch (ReturnSignal& r) {
        ret = std::move(r.value);
    }
    --_callDepth;
    return ret;
}

Value Interpreter::callNative(const std::string& name, NativeArgs args, NativeNamedArgs named) {
    auto it = _natives.find(name);
    if (it == _natives.end()) throw std::runtime_error("Unknown native: " + name);
    return it->second(std::move(args), std::move(named));
}

// ── execStmt ─────────────────────────────────────────────────────────────────

void Interpreter::execStmt(const Stmt& s, std::shared_ptr<Env> env) {
    tick();
    std::visit([&](auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ExprStmt>) {
            evalExpr(*node.expr, env);

        } else if constexpr (std::is_same_v<T, LetStmt>) {
            Value v = node.init ? evalExpr(*node.init, env) : Value::null();
            env->set(node.name, std::move(v));

        } else if constexpr (std::is_same_v<T, BlockStmt>) {
            execBlock(node.body, std::make_shared<Env>(env));

        } else if constexpr (std::is_same_v<T, IfStmt>) {
            if (evalExpr(*node.condition, env).truthy())
                execStmt(*node.thenBranch, env);
            else if (node.elseBranch)
                execStmt(*node.elseBranch, env);

        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            int iters = 0;
            while (evalExpr(*node.condition, env).truthy()) {
                tick();
                if (++iters > MAX_LOOP_ITERS)
                    throw std::runtime_error("Loop iteration limit exceeded");
                try { execStmt(*node.body, env); }
                catch (BreakSignal&)    { break; }
                catch (ContinueSignal&) { continue; }
            }

        } else if constexpr (std::is_same_v<T, ForStmt>) {
            auto loopEnv = std::make_shared<Env>(env);
            double from = evalExpr(*node.from, env).asNumber();
            double to   = evalExpr(*node.to,   env).asNumber();
            loopEnv->set(node.init, Value::number(from));
            int iters = 0;
            for (double i = from; i < to; i += 1.0) {
                tick();
                if (++iters > MAX_LOOP_ITERS)
                    throw std::runtime_error("Loop iteration limit exceeded");
                loopEnv->assign(node.init, Value::number(i));
                try { execStmt(*node.body, loopEnv); }
                catch (BreakSignal&)    { break; }
                catch (ContinueSignal&) { continue; }
            }

        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            Value v = node.value ? evalExpr(*node.value, env) : Value::null();
            throw ReturnSignal{std::move(v)};

        } else if constexpr (std::is_same_v<T, BreakStmt>) {
            throw BreakSignal{};

        } else if constexpr (std::is_same_v<T, ContinueStmt>) {
            throw ContinueSignal{};

        } else if constexpr (std::is_same_v<T, FnDecl>) {
            // fn defined inside a block — register in current env's scope
            ScriptFn sf;
            sf.name    = node.name;
            sf.params  = node.params;
            sf.body    = node.body.get();
            sf.closure = env;
            _fns[node.name] = std::move(sf);

        } else if constexpr (std::is_same_v<T, SpellDecl>) {
            ScriptFn sf;
            sf.name    = node.name;
            sf.params  = node.params;
            sf.body    = node.body.get();
            sf.closure = env;
            _fns[node.name] = std::move(sf);
        }
    }, s.node);
}

void Interpreter::execBlock(const std::vector<StmtPtr>& body, std::shared_ptr<Env> env) {
    for (auto& s : body) execStmt(*s, env);
}

// ── evalExpr ─────────────────────────────────────────────────────────────────

Value Interpreter::evalExpr(const Expr& e, std::shared_ptr<Env> env) {
    tick();
    return std::visit([&](auto& node) -> Value {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, NumberExpr>)
            return Value::number(node.value);

        else if constexpr (std::is_same_v<T, StringExpr>)
            return Value::string(node.value);

        else if constexpr (std::is_same_v<T, BoolExpr>)
            return Value::boolean(node.value);

        else if constexpr (std::is_same_v<T, NullExpr>)
            return Value::null();

        else if constexpr (std::is_same_v<T, IdentExpr>) {
            auto* v = env->lookup(node.name);
            if (!v) throw std::runtime_error("Undefined variable: " + node.name);
            return *v;
        }

        else if constexpr (std::is_same_v<T, BinopExpr>)
            return evalBinop(node, env);

        else if constexpr (std::is_same_v<T, UnaryExpr>) {
            Value v = evalExpr(*node.operand, env);
            if (node.op == "!") return Value::boolean(!v.truthy());
            if (node.op == "-") {
                if (v.isNumber()) return Value::number(-v.asNumber());
                if (v.isVec3()) { auto x=v.asVec3(); return Value::vec3(-x.x,-x.y,-x.z); }
            }
            return Value::null();
        }

        else if constexpr (std::is_same_v<T, AssignExpr>)
            return evalAssign(node, env);

        else if constexpr (std::is_same_v<T, CallExpr>)
            return evalCall(node, env);

        else if constexpr (std::is_same_v<T, Vec3Expr>) {
            float x = (float)evalExpr(*node.x, env).asNumber();
            float y = (float)evalExpr(*node.y, env).asNumber();
            float z = (float)evalExpr(*node.z, env).asNumber();
            return Value::vec3(x, y, z);
        }

        else if constexpr (std::is_same_v<T, MemberExpr>) {
            Value obj = evalExpr(*node.object, env);
            if (obj.isVec3()) {
                auto v = obj.asVec3();
                if (node.member == "x") return Value::number(v.x);
                if (node.member == "y") return Value::number(v.y);
                if (node.member == "z") return Value::number(v.z);
            }
            if (obj.isArray() && node.member == "length")
                return Value::number((double)obj.asArray()->elems.size());
            return Value::null();
        }

        else if constexpr (std::is_same_v<T, IndexExpr>) {
            Value obj = evalExpr(*node.object, env);
            Value idx = evalExpr(*node.index,  env);
            if (obj.isArray() && idx.isNumber()) {
                int i = (int)idx.asNumber();
                auto& elems = obj.asArray()->elems;
                if (i >= 0 && i < (int)elems.size()) return elems[i];
            }
            return Value::null();
        }

        return Value::null();
    }, e.node);
}

// ── evalBinop ────────────────────────────────────────────────────────────────

Value Interpreter::evalBinop(const BinopExpr& e, std::shared_ptr<Env> env) {
    Value l = evalExpr(*e.left, env);

    // Short-circuit logical
    if (e.op == "&&") return Value::boolean(l.truthy() && evalExpr(*e.right, env).truthy());
    if (e.op == "||") return Value::boolean(l.truthy() || evalExpr(*e.right, env).truthy());

    Value r = evalExpr(*e.right, env);

    // Equality
    if (e.op == "==" || e.op == "!=") {
        bool eq = false;
        if (l.isNull() && r.isNull()) eq = true;
        else if (l.isBool()   && r.isBool())   eq = l.asBool()   == r.asBool();
        else if (l.isNumber() && r.isNumber()) eq = l.asNumber() == r.asNumber();
        else if (l.isString() && r.isString()) eq = l.asString() == r.asString();
        return Value::boolean(e.op == "==" ? eq : !eq);
    }

    // Arithmetic
    if (l.isNumber() && r.isNumber()) {
        double a = l.asNumber(), b = r.asNumber();
        if (e.op == "+")  return Value::number(a + b);
        if (e.op == "-")  return Value::number(a - b);
        if (e.op == "*")  return Value::number(a * b);
        if (e.op == "/")  return Value::number(b != 0.0 ? a / b : 0.0);
        if (e.op == "%")  return Value::number(std::fmod(a, b));
        if (e.op == "<")  return Value::boolean(a < b);
        if (e.op == ">")  return Value::boolean(a > b);
        if (e.op == "<=") return Value::boolean(a <= b);
        if (e.op == ">=") return Value::boolean(a >= b);
    }

    // Vec3 arithmetic
    if (l.isVec3() && r.isVec3()) {
        auto a = l.asVec3(), b = r.asVec3();
        if (e.op == "+") return Value::vec3(a.x+b.x, a.y+b.y, a.z+b.z);
        if (e.op == "-") return Value::vec3(a.x-b.x, a.y-b.y, a.z-b.z);
    }
    if (l.isVec3() && r.isNumber()) {
        auto a = l.asVec3(); float b = (float)r.asNumber();
        if (e.op == "*") return Value::vec3(a.x*b, a.y*b, a.z*b);
        if (e.op == "/") return Value::vec3(a.x/b, a.y/b, a.z/b);
    }

    // String concat
    if (l.isString() && e.op == "+") return Value::string(l.asString() + r.toString());

    return Value::null();
}

// ── evalAssign ───────────────────────────────────────────────────────────────

Value Interpreter::evalAssign(const AssignExpr& e, std::shared_ptr<Env> env) {
    Value rhs = evalExpr(*e.value, env);

    if (e.op != "=") {
        // compound: read current, apply op
        Value* cur = env->lookup(e.name);
        if (cur && cur->isNumber() && rhs.isNumber()) {
            double a = cur->asNumber(), b = rhs.asNumber();
            double res = a;
            if      (e.op == "+=") res = a + b;
            else if (e.op == "-=") res = a - b;
            else if (e.op == "*=") res = a * b;
            else if (e.op == "/=") res = b != 0.0 ? a / b : 0.0;
            rhs = Value::number(res);
        }
    }

    if (!env->assign(e.name, rhs)) {
        // variable doesn't exist yet — create it
        env->set(e.name, rhs);
    }
    return rhs;
}

// ── evalCall ─────────────────────────────────────────────────────────────────

Value Interpreter::evalCall(const CallExpr& e, std::shared_ptr<Env> env) {
    // Resolve positional args
    NativeArgs    posArgs;
    NativeNamedArgs namedArgs;
    for (auto& a : e.args)       posArgs.push_back(evalExpr(*a, env));
    for (auto& [k, v] : e.namedArgs) namedArgs[k] = evalExpr(*v, env);

    // Script function?
    auto fit = _fns.find(e.callee);
    if (fit != _fns.end()) {
        // merge named args by position (simple: just pass positional)
        return callScriptFn(fit->second, posArgs);
    }

    // Native?
    auto nit = _natives.find(e.callee);
    if (nit != _natives.end()) return nit->second(posArgs, namedArgs);

    throw std::runtime_error("Unknown function: " + e.callee);
}

} // namespace Aether