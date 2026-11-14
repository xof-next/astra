#pragma once
#include <string>
#include <vector>
namespace astra {
enum class TokenKind { Identifier, String, Number, Symbol, End };
struct Token { TokenKind kind; std::string text; int line, column; };
class Lexer {
public:
    explicit Lexer(std::string source): source_(std::move(source)) {}
    std::vector<Token> scan();
    const std::vector<std::string>& errors() const { return errors_; }
private:
    std::string source_;
    std::vector<std::string> errors_;
};
}
