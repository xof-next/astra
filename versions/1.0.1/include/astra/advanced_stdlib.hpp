#pragma once
#include "astra/value.hpp"
#include <string>

namespace astra {
// Returns Undefined when the requested native export is not provided here.
Value createAdvancedBuiltin(const std::string& qualifiedName);
// Stateful standard modules (currently std:events) are created as a namespace.
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
