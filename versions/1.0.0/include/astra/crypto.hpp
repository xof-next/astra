#pragma once
#include <string>
namespace astra {
std::string sha256Hex(const std::string& input);
std::string hmacSha256Hex(const std::string& key,const std::string& input);
bool constantTimeEqual(const std::string& a,const std::string& b);
}
