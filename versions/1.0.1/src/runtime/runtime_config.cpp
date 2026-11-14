#include "astra/runtime_config.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#include <stdexcept>

namespace astra {
namespace {
std::string trimConfig(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lowerConfig(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string stripConfigComment(const std::string& value) {
    bool quoted = false;
    bool escaped = false;
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (escaped) { escaped = false; continue; }
        if (quoted && c == '\\') { escaped = true; continue; }
        if (c == '"') { quoted = !quoted; continue; }
        if (!quoted && (c == '#' || c == ';')) return trimConfig(value.substr(0, i));
    }
    if (quoted || escaped) throw std::runtime_error("unterminated quoted value");
    return trimConfig(value);
}

std::string parseConfigValue(const std::string& raw) {
    const std::string value = stripConfigComment(raw);
    if (value.empty()) throw std::runtime_error("configuration value cannot be empty");
    if (value.front() != '"') return value;
    if (value.size() < 2 || value.back() != '"') throw std::runtime_error("quoted value must end with a quote");
    std::string out;
    out.reserve(value.size() - 2);
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\') {
            if (i + 2 >= value.size()) throw std::runtime_error("invalid escape sequence");
            const char next = value[++i];
            if (next != '\\' && next != '"') throw std::runtime_error("unsupported quoted escape sequence");
            out.push_back(next);
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

long long parseNumeric(const std::string& raw, const std::string& label) {
    if (raw.empty()) throw std::runtime_error(label + " cannot be empty");
    size_t used = 0;
    long long value = 0;
    try { value = std::stoll(raw, &used, 10); }
    catch (...) { throw std::runtime_error(label + " must be a valid integer"); }
    if (used != raw.size()) throw std::runtime_error(label + " must be a valid integer");
    return value;
}

long long parseScaled(const std::string& raw, const std::string& label, bool duration) {
    std::string value = lowerConfig(trimConfig(raw));
    size_t split = 0;
    if (split < value.size() && (value[split] == '+' || value[split] == '-')) ++split;
    while (split < value.size() && std::isdigit(static_cast<unsigned char>(value[split]))) ++split;
    const long long amount = parseNumeric(value.substr(0, split), label);
    const std::string unit = value.substr(split);
    long long multiplier = 1;
    if (duration) {
        if (unit.empty() || unit == "ms") multiplier = 1;
        else if (unit == "s") multiplier = 1000;
        else if (unit == "m") multiplier = 60000;
        else throw std::runtime_error(label + " must use ms, s, or m units");
    } else {
        if (unit.empty() || unit == "b") multiplier = 1;
        else if (unit == "kb" || unit == "kib") multiplier = 1024;
        else if (unit == "mb" || unit == "mib") multiplier = 1024LL * 1024LL;
        else if (unit == "gb" || unit == "gib") multiplier = 1024LL * 1024LL * 1024LL;
        else throw std::runtime_error(label + " must use B, KB, MB, or GB units");
    }
    if (amount < 0 || amount > std::numeric_limits<long long>::max() / multiplier)
        throw std::runtime_error(label + " is outside the supported range");
    return amount * multiplier;
}

bool knownKey(const std::string& section, const std::string& key) {
    static const std::map<std::string, std::vector<std::string>> allowed = {
        {"server", {"host", "workers", "queue_capacity", "backlog", "max_connections", "max_connections_per_ip", "max_request_buffer", "max_response_buffer", "max_header_buffer", "request_timeout", "max_tracked_ips"}},
        {"rate_limit", {"enabled", "window", "max_requests_total", "max_requests_per_ip"}},
        {"client", {"max_request_buffer", "max_response_buffer", "max_header_buffer", "timeout", "connect_timeout", "max_redirects", "verify_tls"}}
    };
    auto it = allowed.find(section);
    return it != allowed.end() && std::find(it->second.begin(), it->second.end(), key) != it->second.end();
}
}

RuntimeConfig::RuntimeConfig() {
    const char* overridePath = std::getenv("ASTRA_CONFIG");
    std::filesystem::path path;
    if (overridePath && *overridePath) {
        path = std::filesystem::u8path(overridePath);
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("ASTRA_CONFIG points to a missing configuration file: " + path.string());
    } else {
        path = std::filesystem::current_path() / "astra.conf";
        if (!std::filesystem::is_regular_file(path)) return;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open runtime configuration: " + path.string());
    sourcePath_ = path.string();
    loadedFromFile_ = true;
    std::string section;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        line = trimConfig(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') {
            if (line.size() < 3 || line.back() != ']')
                throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": invalid section header");
            section = lowerConfig(trimConfig(line.substr(1, line.size() - 2)));
            if (section.empty()) throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": empty section name");
            if (section != "server" && section != "rate_limit" && section != "client")
                throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": unknown section [" + section + "]");
            continue;
        }
        const size_t equal = line.find('=');
        if (equal == std::string::npos || section.empty())
            throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": expected key = value inside a section");
        const std::string key = lowerConfig(trimConfig(line.substr(0, equal)));
        if (key.empty() || !std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
            throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": invalid key name");
        if (!knownKey(section, key))
            throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": unknown setting [" + section + "]." + key);
        const std::string fullKey = section + "." + key;
        if (values_.count(fullKey))
            throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": duplicate setting " + fullKey);
        try {
            values_.emplace(fullKey, parseConfigValue(line.substr(equal + 1)));
        } catch (const std::exception& error) {
            throw std::runtime_error(sourcePath_ + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (!input.eof()) throw std::runtime_error("Failed while reading runtime configuration: " + sourcePath_);
    try {
        if (stringValue("server", "host", "127.0.0.1").empty()) throw std::runtime_error("[server].host cannot be empty");
        integerValue("server", "workers", 0, 0, 128);
        integerValue("server", "queue_capacity", 512, 1, 65536);
        integerValue("server", "backlog", 256, 1, 65535);
        integerValue("server", "max_connections", 1024, 1, 65536);
        integerValue("server", "max_connections_per_ip", 64, 1, 65536);
        sizeValue("server", "max_request_buffer", 1024u * 1024u, 1, 64u * 1024u * 1024u);
        sizeValue("server", "max_response_buffer", 4u * 1024u * 1024u, 1, 64u * 1024u * 1024u);
        sizeValue("server", "max_header_buffer", 64u * 1024u, 1024, 1024u * 1024u);
        durationMs("server", "request_timeout", 10000, 100, 120000);
        integerValue("server", "max_tracked_ips", 16384, 128, 1048576);
        boolValue("rate_limit", "enabled", true);
        durationMs("rate_limit", "window", 1000, 100, 60000);
        integerValue("rate_limit", "max_requests_total", 5000, 1, 10000000);
        integerValue("rate_limit", "max_requests_per_ip", 2000, 1, 1000000);
        sizeValue("client", "max_request_buffer", 10u * 1024u * 1024u, 1, 64u * 1024u * 1024u);
        sizeValue("client", "max_response_buffer", 10u * 1024u * 1024u, 1, 64u * 1024u * 1024u);
        sizeValue("client", "max_header_buffer", 256u * 1024u, 1024, 1024u * 1024u);
        durationMs("client", "timeout", 15000, 100, 120000);
        durationMs("client", "connect_timeout", 5000, 100, 60000);
        integerValue("client", "max_redirects", 5, 0, 20);
        boolValue("client", "verify_tls", true);
    } catch (const std::exception& error) {
        throw std::runtime_error(sourcePath_ + ": " + error.what());
    }
}

const RuntimeConfig& RuntimeConfig::instance() {
    static const RuntimeConfig config;
    return config;
}

std::string RuntimeConfig::stringValue(const std::string& section, const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(lowerConfig(section) + "." + lowerConfig(key));
    return it == values_.end() ? fallback : it->second;
}

long RuntimeConfig::integerValue(const std::string& section, const std::string& key, long fallback, long minimum, long maximum) const {
    const auto it = values_.find(lowerConfig(section) + "." + lowerConfig(key));
    if (it == values_.end()) return fallback;
    const long long value = parseNumeric(it->second, "[" + section + "]." + key);
    if (value < minimum || value > maximum) throw std::runtime_error("[" + section + "]." + key + " must be between " + std::to_string(minimum) + " and " + std::to_string(maximum));
    return static_cast<long>(value);
}

size_t RuntimeConfig::sizeValue(const std::string& section, const std::string& key, size_t fallback, size_t minimum, size_t maximum) const {
    const auto it = values_.find(lowerConfig(section) + "." + lowerConfig(key));
    if (it == values_.end()) return fallback;
    const long long value = parseScaled(it->second, "[" + section + "]." + key, false);
    if (value < 0 || static_cast<unsigned long long>(value) < minimum || static_cast<unsigned long long>(value) > maximum)
        throw std::runtime_error("[" + section + "]." + key + " is outside the supported range");
    return static_cast<size_t>(value);
}

long RuntimeConfig::durationMs(const std::string& section, const std::string& key, long fallback, long minimum, long maximum) const {
    const auto it = values_.find(lowerConfig(section) + "." + lowerConfig(key));
    if (it == values_.end()) return fallback;
    const long long value = parseScaled(it->second, "[" + section + "]." + key, true);
    if (value < minimum || value > maximum) throw std::runtime_error("[" + section + "]." + key + " must be between " + std::to_string(minimum) + " and " + std::to_string(maximum) + " ms");
    return static_cast<long>(value);
}

bool RuntimeConfig::boolValue(const std::string& section, const std::string& key, bool fallback) const {
    const auto it = values_.find(lowerConfig(section) + "." + lowerConfig(key));
    if (it == values_.end()) return fallback;
    const std::string value = lowerConfig(it->second);
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("[" + section + "]." + key + " must be true or false");
}

bool RuntimeConfig::loadedFromFile() const { return loadedFromFile_; }
const std::string& RuntimeConfig::sourcePath() const { return sourcePath_; }
}
