#include "astra/advanced_stdlib.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <mutex>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra { namespace {
using Args = std::vector<Value>;
struct HttpResponse {
    std::string body;
    std::map<std::string, std::string> headers;
    std::string effectiveUrl;
    long status = 0;
    double elapsedMs = 0;
    size_t maxBytes = 10u * 1024u * 1024u;
    size_t headerBytes = 0;
    bool tooLarge = false;
    bool headersTooLarge = false;
};

Value undefinedValue() { return Value{}; }
Value fn(NativeFunction f) { return Value(std::move(f)); }
std::shared_ptr<Object> makeObject() { return std::make_shared<Object>(); }
void requireArgs(const Args& args, size_t n, const std::string& name) {
    if (args.size() < n) throw std::runtime_error(name + " expects at least " + std::to_string(n) + " argument(s)");
}
std::string argString(const Args& args, size_t i, const std::string& name) {
    if (i >= args.size()) throw std::runtime_error(name + " is missing");
    if (!std::holds_alternative<std::string>(args[i].data))
        throw std::runtime_error(name + " must be a string");
    return std::get<std::string>(args[i].data);
}
const std::shared_ptr<Object>* asObject(const Value& v) {
    return std::get_if<std::shared_ptr<Object>>(&v.data);
}
Value option(const Value& opts, const std::string& key) {
    auto p = asObject(opts);
    if (!p) return Value{};
    auto it = (*p)->fields.find(key);
    return it == (*p)->fields.end() ? Value{} : it->second;
}
bool optionBool(const Value& opts, const std::string& key, bool fallback) {
    Value v = option(opts, key);
    if (std::holds_alternative<Undefined>(v.data)) return fallback;
    if (auto p = std::get_if<bool>(&v.data)) return *p;
    throw std::runtime_error("launch option `" + key + "` must be boolean");
}
long optionLong(const Value& opts, const std::string& key, long fallback, long minimum, long maximum) {
    Value v = option(opts, key);
    if (std::holds_alternative<Undefined>(v.data)) return fallback;
    int64_t n = 0;
    if (auto p = std::get_if<int64_t>(&v.data)) n = *p;
    else throw std::runtime_error("launch option `" + key + "` must be an integer");
    if (n < minimum || n > maximum)
        throw std::runtime_error("launch option `" + key + "` must be between " + std::to_string(minimum) + " and " + std::to_string(maximum));
    return static_cast<long>(n);
}
std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trimAscii(std::string s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}
size_t writeBody(char* ptr, size_t size, size_t count, void* userdata) {
    auto* response = static_cast<HttpResponse*>(userdata);
    if (size != 0 && count > SIZE_MAX / size) return 0;
    const size_t bytes = size * count;
    if (bytes > response->maxBytes - std::min(response->body.size(), response->maxBytes)) {
        response->tooLarge = true;
        return 0;
    }
    response->body.append(ptr, bytes);
    return bytes;
}
size_t writeHeader(char* ptr, size_t size, size_t count, void* userdata) {
    auto* response = static_cast<HttpResponse*>(userdata);
    if (size != 0 && count > SIZE_MAX / size) return 0;
    const size_t bytes = size * count;
    if (bytes > 256u * 1024u - std::min(response->headerBytes, size_t(256u * 1024u))) {
        response->headersTooLarge = true;
        return 0;
    }
    response->headerBytes += bytes;
    std::string line(ptr, bytes);
    if (line.rfind("HTTP/", 0) == 0) {
        response->headers.clear();
        response->body.clear();
        return bytes;
    }
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = lowerAscii(trimAscii(line.substr(0, colon)));
        std::string value = trimAscii(line.substr(colon + 1));
        if (!key.empty()) {
            auto it = response->headers.find(key);
            if (it == response->headers.end()) response->headers.emplace(std::move(key), std::move(value));
            else it->second += ", " + value;
        }
    }
    return bytes;
}

std::once_flag curlInitFlag;
CURLcode ensureCurl() {
    static CURLcode initResult = CURLE_FAILED_INIT;
    std::call_once(curlInitFlag, [] { initResult = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return initResult;
}
struct CurlHandle {
    CURL* value = curl_easy_init();
    ~CurlHandle() { if (value) curl_easy_cleanup(value); }
};
struct HeaderList {
    curl_slist* value = nullptr;
    ~HeaderList() { if (value) curl_slist_free_all(value); }
    void add(const std::string& s) {
        curl_slist* next = curl_slist_append(value, s.c_str());
        if (!next) throw std::runtime_error("Unable to allocate HTTP headers");
        value = next;
    }
};

HttpResponse perform(const std::string& method, const std::string& url, const std::string& body,
                     const Value& options, bool headOnly = false) {
    if (!std::holds_alternative<Undefined>(options.data) && !std::holds_alternative<Null>(options.data) && !asObject(options))
        throw std::runtime_error("launch request options must be an object");
    if (std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f; }))
        throw std::runtime_error("URL must not contain whitespace or control characters");
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        throw std::runtime_error("Only http:// and https:// URLs are supported");
    if (ensureCurl() != CURLE_OK) throw std::runtime_error("HTTP runtime initialization failed");
    CurlHandle handle;
    if (!handle.value) throw std::runtime_error("Unable to create HTTP request handle");

    HttpResponse response;
    response.maxBytes = static_cast<size_t>(optionLong(options, "maxResponseBytes", 10L * 1024L * 1024L, 1, 64L * 1024L * 1024L));
    const long timeoutMs = optionLong(options, "timeoutMs", 15000, 100, 120000);
    const long connectTimeoutMs = optionLong(options, "connectTimeoutMs", 5000, 100, 60000);
    const long maxRedirects = optionLong(options, "maxRedirects", 5, 0, 20);
    const bool followRedirects = optionBool(options, "followRedirects", true);
    const bool verifyTls = optionBool(options, "verifyTls", true);
    const std::string userAgent = [&] {
        Value v = option(options, "userAgent");
        if (std::holds_alternative<Undefined>(v.data)) return std::string("Astra/1.0.0");
        if (auto p = std::get_if<std::string>(&v.data)) return *p;
        throw std::runtime_error("launch option `userAgent` must be a string");
    }();

    curl_easy_setopt(handle.value, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.value, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.value, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(handle.value, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
    curl_easy_setopt(handle.value, CURLOPT_FOLLOWLOCATION, followRedirects ? 1L : 0L);
    curl_easy_setopt(handle.value, CURLOPT_MAXREDIRS, maxRedirects);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle.value, CURLOPT_PROTOCOLS_STR, "http,https");
    // Do not allow an HTTPS request to be downgraded by a redirect.
    curl_easy_setopt(handle.value, CURLOPT_REDIR_PROTOCOLS_STR, url.rfind("https://", 0) == 0 ? "https" : "http,https");
#else
    curl_easy_setopt(handle.value, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(handle.value, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(url.rfind("https://", 0) == 0 ? CURLPROTO_HTTPS : (CURLPROTO_HTTP | CURLPROTO_HTTPS)));
#endif
    curl_easy_setopt(handle.value, CURLOPT_SSL_VERIFYPEER, verifyTls ? 1L : 0L);
    curl_easy_setopt(handle.value, CURLOPT_SSL_VERIFYHOST, verifyTls ? 2L : 0L);
    curl_easy_setopt(handle.value, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    std::string caInfo;
    Value caInfoValue = option(options, "caInfo");
    if (!std::holds_alternative<Undefined>(caInfoValue.data)) {
        if (auto p = std::get_if<std::string>(&caInfoValue.data)) caInfo = *p;
        else throw std::runtime_error("launch option `caInfo` must be a filesystem path string");
        curl_easy_setopt(handle.value, CURLOPT_CAINFO, caInfo.c_str());
    }
    curl_easy_setopt(handle.value, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(handle.value, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle.value, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(handle.value, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(handle.value, CURLOPT_HEADERFUNCTION, writeHeader);
    curl_easy_setopt(handle.value, CURLOPT_HEADERDATA, &response);
    curl_easy_setopt(handle.value, CURLOPT_NOBODY, headOnly ? 1L : 0L);

    HeaderList headers;
    Value hv = option(options, "headers");
    if (!std::holds_alternative<Undefined>(hv.data)) {
        auto objectPtr = asObject(hv);
        if (!objectPtr) throw std::runtime_error("launch option `headers` must be an object");
        for (const auto& item : (*objectPtr)->fields) {
            const bool validName = !item.first.empty() && std::all_of(item.first.begin(), item.first.end(), [](unsigned char c) {
                return std::isalnum(c) || std::string("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string::npos;
            });
            if (!validName) throw std::runtime_error("Invalid HTTP header name");
            const std::string value = valueToString(item.second);
            const bool validValue = std::none_of(value.begin(), value.end(), [](unsigned char c) {
                return (c < 0x20 && c != '\t') || c == 0x7f;
            });
            if (!validValue) throw std::runtime_error("Invalid HTTP header value");
            headers.add(item.first + ": " + value);
        }
    }
    if (headers.value) curl_easy_setopt(handle.value, CURLOPT_HTTPHEADER, headers.value);

    if (headOnly || method == "HEAD") {
        curl_easy_setopt(handle.value, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(handle.value, CURLOPT_CUSTOMREQUEST, "HEAD");
    } else if (method == "GET") {
        curl_easy_setopt(handle.value, CURLOPT_HTTPGET, 1L);
    } else if (method == "POST") {
        curl_easy_setopt(handle.value, CURLOPT_POST, 1L);
        curl_easy_setopt(handle.value, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle.value, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    } else {
        curl_easy_setopt(handle.value, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty()) {
            curl_easy_setopt(handle.value, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(handle.value, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        }
    }

    const CURLcode code = curl_easy_perform(handle.value);
    if (code != CURLE_OK) {
        if (response.tooLarge) throw std::runtime_error("HTTP response exceeded maxResponseBytes limit");
        if (response.headersTooLarge) throw std::runtime_error("HTTP response headers exceeded 256 KiB safety limit");
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(code));
    }
    curl_easy_getinfo(handle.value, CURLINFO_RESPONSE_CODE, &response.status);
    char* effectiveUrl = nullptr;
    curl_easy_getinfo(handle.value, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    if (effectiveUrl) response.effectiveUrl = effectiveUrl;
    double totalSeconds = 0.0;
    curl_easy_getinfo(handle.value, CURLINFO_TOTAL_TIME, &totalSeconds);
    response.elapsedMs = totalSeconds * 1000.0;
    return response;
}

Value responseValue(const HttpResponse& response, const std::string& method, const std::string& originalUrl) {
    auto out = makeObject();
    auto headers = makeObject();
    for (const auto& h : response.headers) headers->fields[h.first] = Value(h.second);
    out->fields["status"] = Value(static_cast<int64_t>(response.status));
    out->fields["ok"] = Value(response.status >= 200 && response.status < 300);
    out->fields["headers"] = Value(headers);
    out->fields["body"] = Value(response.body);
    out->fields["text"] = Value(response.body);
    out->fields["url"] = Value(response.effectiveUrl);
    out->fields["redirected"] = Value(!response.effectiveUrl.empty() && response.effectiveUrl != originalUrl);
    out->fields["method"] = Value(method);
    out->fields["elapsedMs"] = Value(response.elapsedMs);
    auto it = response.headers.find("content-type");
    out->fields["contentType"] = Value(it == response.headers.end() ? std::string{} : it->second);
    return Value(out);
}

std::string upperAscii(std::string s);
Value launchRequest(const std::string& method, const Args& args, const std::string& exportName) {
    requireArgs(args, 1, "launch." + exportName);
    const size_t maxArgs = exportName == "request" ? 2u : ((exportName == "get" || exportName == "head") ? 2u : 3u);
    if (args.size() > maxArgs) throw std::runtime_error("launch." + exportName + " received too many arguments");
    const std::string url = argString(args, 0, "url");
    std::string body;
    Value options;
    if (exportName == "request") {
        if (args.size() > 1) options = args[1];
        Value methodOpt = option(options, "method");
        std::string actualMethod = method;
        if (!std::holds_alternative<Undefined>(methodOpt.data)) {
            if (auto p = std::get_if<std::string>(&methodOpt.data)) actualMethod = *p;
            else throw std::runtime_error("launch option `method` must be a string");
        }
        actualMethod = upperAscii(actualMethod);
        if (actualMethod != "GET" && actualMethod != "POST" && actualMethod != "PUT" && actualMethod != "PATCH" && actualMethod != "DELETE" && actualMethod != "HEAD" && actualMethod != "OPTIONS")
            throw std::runtime_error("Unsupported HTTP method `" + actualMethod + "`");
        Value bodyOpt = option(options, "body");
        if (!std::holds_alternative<Undefined>(bodyOpt.data) && !std::holds_alternative<Null>(bodyOpt.data)) {
            if (!std::holds_alternative<std::string>(bodyOpt.data)) throw std::runtime_error("launch option `body` must be a string; serialize JSON explicitly with json.stringify()");
            body = std::get<std::string>(bodyOpt.data);
        }
        if ((actualMethod == "GET" || actualMethod == "HEAD") && !body.empty())
            throw std::runtime_error("HTTP " + actualMethod + " requests cannot have a body in this Astra runtime");
        auto r = perform(actualMethod, url, body, options, actualMethod == "HEAD");
        return responseValue(r, actualMethod, url);
    }
    if (exportName == "get" || exportName == "head") {
        if (args.size() > 1) options = args[1];
    } else {
        if (args.size() > 1 && !std::holds_alternative<Null>(args[1].data) && !std::holds_alternative<Undefined>(args[1].data)) {
            if (std::holds_alternative<std::string>(args[1].data)) {
                body = std::get<std::string>(args[1].data);
                if (args.size() > 2) options = args[2];
            } else if (asObject(args[1])) {
                options = args[1];
                Value bodyOpt = option(options, "body");
                if (!std::holds_alternative<Undefined>(bodyOpt.data) && !std::holds_alternative<Null>(bodyOpt.data)) {
                    if (!std::holds_alternative<std::string>(bodyOpt.data)) throw std::runtime_error("launch option `body` must be a string; serialize JSON explicitly with json.stringify()");
                    body = std::get<std::string>(bodyOpt.data);
                }
            } else {
                throw std::runtime_error("Request body must be a string or the second argument must be an options object");
            }
        } else if (args.size() > 2) {
            options = args[2];
        }
    }
    std::string actualMethod = method;
    auto r = perform(actualMethod, url, body, options, actualMethod == "HEAD");
    return responseValue(r, actualMethod, url);
}
std::string upperAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

Value createAdvancedBuiltin(const std::string& name) {
    Value networkBuiltin = createNetworkBuiltin(name);
    if (!std::holds_alternative<Undefined>(networkBuiltin.data)) return networkBuiltin;
    Value serverBuiltin = createServerBuiltin(name);
    if (!std::holds_alternative<Undefined>(serverBuiltin.data)) return serverBuiltin;
    Value webBuiltin = createWebBuiltin(name);
    if (!std::holds_alternative<Undefined>(webBuiltin.data)) return webBuiltin;
    Value collectionsBuiltin = createCollectionsBuiltin(name);
    if (!std::holds_alternative<Undefined>(collectionsBuiltin.data)) return collectionsBuiltin;
    if (name.rfind("std:launch.", 0) == 0) {
        const std::string exportName = name.substr(std::string("std:launch.").size());
        static const std::vector<std::string> methods = {"request", "get", "post", "put", "patch", "del", "delete", "head", "options"};
        if (std::find(methods.begin(), methods.end(), exportName) == methods.end()) return undefinedValue();
        const std::string method = (exportName == "del" || exportName == "delete") ? "DELETE" : exportName == "request" ? "GET" : upperAscii(exportName);
        return fn([method, exportName](const Args& args) { return launchRequest(method, args, exportName); });
    }
    if (name == "std:tls.backend") return fn([](const Args& args) {
        if (!args.empty()) throw std::runtime_error("tls.backend expects no arguments");
        if (ensureCurl() != CURLE_OK) return Value(std::string("unavailable"));
        const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        if (!info || !(info->features & CURL_VERSION_SSL) || !info->ssl_version) return Value(std::string("unavailable"));
        return Value(std::string(info->ssl_version));
    });
    if (name == "std:tls.supported") return fn([](const Args& args) {
        if (!args.empty()) throw std::runtime_error("tls.supported expects no arguments");
        if (ensureCurl() != CURLE_OK) return Value(false);
        const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        return Value(info && (info->features & CURL_VERSION_SSL));
    });
    if (name == "std:tls.policy") return fn([](const Args& args) {
        if (!args.empty()) throw std::runtime_error("tls.policy expects no arguments");
        auto out = makeObject();
        out->fields["minimumVersion"] = Value(std::string("TLS 1.2"));
        out->fields["verifyCertificate"] = Value(true);
        out->fields["verifyHostname"] = Value(true);
        out->fields["allowedProtocols"] = Value(std::string("HTTPS only for TLS operations"));
        return Value(out);
    });
    if (name == "std:tls.verify") return fn([](const Args& args) {
        requireArgs(args, 1, "tls.verify");
        if (args.size() > 2) throw std::runtime_error("tls.verify accepts at most a URL and a CA certificate path");
        const std::string url = argString(args, 0, "url");
        if (url.rfind("https://", 0) != 0) throw std::runtime_error("tls.verify requires an https:// URL");
        Value options;
        if (args.size() > 1) {
            if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("tls.verify second argument must be a CA certificate path string");
            auto opts = makeObject(); opts->fields["caInfo"] = args[1]; options = Value(opts);
        }
        auto response = perform("HEAD", url, {}, options, true);
        auto out = makeObject();
        out->fields["verified"] = Value(true);
        out->fields["status"] = Value(static_cast<int64_t>(response.status));
        out->fields["url"] = Value(response.effectiveUrl);
        const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        out->fields["backend"] = Value(info && info->ssl_version ? std::string(info->ssl_version) : std::string("unknown"));
        return Value(out);
    });
    Value extra = createBufferBuiltin(name);
    if (!std::holds_alternative<Undefined>(extra.data)) return extra;
    extra = createCompressionBuiltin(name);
    if (!std::holds_alternative<Undefined>(extra.data)) return extra;
    extra = createQueryBuiltin(name);
    if (!std::holds_alternative<Undefined>(extra.data)) return extra;
    extra = createValidationBuiltin(name);
    if (!std::holds_alternative<Undefined>(extra.data)) return extra;
    return undefinedValue();
}

} // namespace astra
