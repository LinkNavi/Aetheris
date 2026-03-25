#pragma once
// Single include for game code — pulls in everything needed to compile
// and run AetherScript spells/runes.
//
// Usage:
//   #include "aether.h"
//
//   Aether::Script script;
//   script.loadFile("spells/fireball.aes");
//   script.vm.registerNative("aoe_damage", [](auto args, auto named) {
//       // hook into your combat system here
//       return Aether::Value::null();
//   });
//   auto result = script.call("fireball", {casterVal, targetPosVal});
//   if (!result.ok) Log::warn(result.error);

#include "aether_lexer.h"
#include "aether_parser.h"
#include "aether_vm.h"
#include <string>
#include <fstream>
#include <sstream>

namespace Aether {

    struct Script {
        Interpreter      vm;
        Program          prog;
        std::string      lastError;

        // Load and compile from a file
        bool loadFile(const std::string& path) {
            std::ifstream f(path);
            if (!f) { lastError = "Cannot open: " + path; return false; }
            std::ostringstream ss; ss << f.rdbuf();
            return loadSource(ss.str());
        }

        // Load and compile from a source string
        bool loadSource(const std::string& src) {
            Lexer lex(src);
            auto tokens = lex.tokenize();
            Parser parser(std::move(tokens));
            prog = parser.parse();
            if (parser.hasError()) { lastError = parser.error(); return false; }
            vm.loadProgram(&prog);
            lastError.clear();
            return true;
        }

        // Call a named function/spell with args
        ExecResult call(const std::string& name, std::vector<Value> args = {}) {
            return vm.call(name, std::move(args));
        }

        bool hasFn(const std::string& name) const { return vm.hasFn(name); }
    };

} // namespace Aether