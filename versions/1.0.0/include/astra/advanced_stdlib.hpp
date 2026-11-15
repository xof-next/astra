#pragma once
#include "astra/value.hpp"
#include <string>

namespace astra {

Value createAdvancedBuiltin(const std::string& qualifiedName);

Value createAdvancedModule(const std::string& moduleName);
Value createBufferBuiltin(const std::string& qualifiedName);
Value createCompressionBuiltin(const std::string& qualifiedName);
Value createQueryBuiltin(const std::string& qualifiedName);
Value createValidationBuiltin(const std::string& qualifiedName);
Value createWebBuiltin(const std::string& qualifiedName);
Value createCollectionsBuiltin(const std::string& qualifiedName);
Value createServerBuiltin(const std::string& qualifiedName);
Value createServerModule(const std::string& moduleName);
Value createNetworkBuiltin(const std::string& qualifiedName);
Value createNetworkModule(const std::string& moduleName);
}
