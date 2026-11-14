#pragma once
#include <string>
#include <vector>
namespace astra {
struct Module { std::string name; std::vector<std::string> exports; };
const std::vector<Module>& builtinModules();
bool hasModule(const std::string&);
bool hasExport(const std::string&, const std::string&);
}
