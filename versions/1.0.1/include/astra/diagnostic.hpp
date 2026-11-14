#pragma once
#include <iostream>
#include <string>
namespace astra {
struct Diagnostic {
    std::string code;
    std::string message;
    std::string file;
    std::string source;
    int line=1;
    int column=1;
};
inline std::string diagnosticHelp(const std::string& code) {
    if (code=="E1001" || code=="E1002") return "Check the syntax near the highlighted token.";
    if (code=="E2001") return "Declare the variable before using it.";
    if (code=="E2002") return "Use `let` if the value must be reassigned.";
    if (code=="E3001") return "Check the module path and make sure the module exists.";
    if (code=="E3002") return "Check the module's exported members; Astra does not invent APIs.";
    if (code=="E3003") return "Only built-in modules are loadable in this preview.";
    if (code=="E4001" || code=="E4004") return "Convert values explicitly and avoid mixing incompatible types.";
    if (code=="E5001") return "Pass the required number of arguments.";
    if (code=="E6001") return "Ensure the divisor is not zero.";
    if (code=="E6002") return "Use `bigint` for integers outside the int64 range.";
    if (code=="E6003") return "Check the value or use optional chaining (`?.`).";
    if (code=="E9001") return "Check the path and file permissions.";
    return {};
}
inline void report(const Diagnostic& d) {
    std::cerr << "error[" << d.code << "]: " << d.message << "\n";
    std::cerr << "  --> " << d.file << ":" << d.line << ":" << d.column << "\n";
    if (!d.source.empty()) {
        std::cerr << "   |\n " << d.line << " | " << d.source << "\n";
        std::cerr << "   | " << std::string(d.column>1?static_cast<size_t>(d.column-1):0,' ') << "^\n";
    }
    const auto help=diagnosticHelp(d.code);
    if (!help.empty()) std::cerr << "   = help: " << help << "\n";
}
}
