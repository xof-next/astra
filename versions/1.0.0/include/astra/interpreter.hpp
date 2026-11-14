#pragma once
#include <string>
namespace astra {
struct RunOptions { bool verbose=false; };
bool runFile(const std::string& path, const RunOptions& options = {});
}
