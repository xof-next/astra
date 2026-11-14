#pragma once
#include <cstddef>
#include <map>
#include <string>

namespace astra {
class RuntimeConfig {
public:
    static const RuntimeConfig& instance();
    std::string stringValue(const std::string& section, const std::string& key, const std::string& fallback) const;
    long integerValue(const std::string& section, const std::string& key, long fallback, long minimum, long maximum) const;
    size_t sizeValue(const std::string& section, const std::string& key, size_t fallback, size_t minimum, size_t maximum) const;
    long durationMs(const std::string& section, const std::string& key, long fallback, long minimum, long maximum) const;
    bool boolValue(const std::string& section, const std::string& key, bool fallback) const;
    bool loadedFromFile() const;
    const std::string& sourcePath() const;

private:
    RuntimeConfig();
    std::map<std::string, std::string> values_;
    std::string sourcePath_;
    bool loadedFromFile_ = false;
};
}
