#pragma once
#include <string>
namespace astra {
int installBuiltRuntime(const std::string& sourcePath);
int installRuntimeVersion(const std::string& version, bool update);
int useInstalledVersion(const std::string& version);
int listAvailableVersions();
std::string currentVersion();
std::string installedRuntimePath();
}
