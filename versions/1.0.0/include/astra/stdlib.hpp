#pragma once
#include "astra/value.hpp"
#include <string>
#include <vector>
namespace astra {
Value createBuiltinModule(const std::string& name, const std::vector<std::string>& args);
Value createGlobal(const std::string& name, const std::vector<std::string>& args);
}
