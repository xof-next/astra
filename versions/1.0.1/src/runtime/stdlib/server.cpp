#include "astra/advanced_stdlib.hpp"
#include "astra/runtime_config.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <iostream>
#include <sstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include <chrono>

#if defined(_WIN32)
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using AstraSocket = SOCKET;
  static constexpr AstraSocket kInvalidSocket = INVALID_SOCKET;
#else
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <poll.h>
  #include <unistd.h>
  using AstraSocket = int;
  static constexpr AstraSocket kInvalidSocket = -1;
#endif

namespace astra { namespace {
using Args = std::vector<Value>;
struct Route { std::string method; std::string path; Value handler; };
struct ServerState { std::vector<Route> routes; std::mutex handlerMutex; };
struct ResponseState { long status = 200; std::map<std::string, std::string> headers; std::string body; bool ended = false; };
struct PendingConnection { AstraSocket socket; std::string ip; };
struct IpLimitState {
    size_t active = 0;
    uint64_t requests = 0;
    std::chrono::steady_clock::time_point windowStart{};
};

Value native(NativeFunction f) { return Value(std::move(f)); }
std::shared_ptr<Object> object() { return std::make_shared<Object>(); }
void need(const Args& args, size_t n, const std::string& name) { if (args.size() < n) throw std::runtime_error(name + " expects at least " + std::to_string(n) + " argument(s)"); }
std::string stringArg(const Args& args, size_t i, const std::string& name) { if (i >= args.size() || !std::holds_alternative<std::string>(args[i].data)) throw std::runtime_error(name + " expects a string argument"); return std::get<std::string>(args[i].data); }
long integerArg(const Value& v, const std::string& label, long low, long high, long fallback) {
    if (std::holds_alternative<Undefined>(v.data)) return fallback;
    auto p = std::get_if<int64_t>(&v.data); if (!p || *p < low || *p > high) throw std::runtime_error(label + " must be an integer between " + std::to_string(low) + " and " + std::to_string(high));
    return static_cast<long>(*p);
}
Value field(const Value& v, const std::string& key) {
    auto p = std::get_if<std::shared_ptr<Object>>(&v.data); if (!p || !*p) return Value{};
    auto it = (*p)->fields.find(key); return it == (*p)->fields.end() ? Value{} : it->second;
}
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string upper(std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
bool safeHeaderName(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || std::string("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string::npos; }); }
bool safeHeaderValue(const std::string& s) { return std::none_of(s.begin(), s.end(), [](unsigned char c) { return (c < 0x20 && c != '\t') || c == 0x7f; }); }

#if defined(_WIN32)
using AstraSockLen = int;
bool socketStartup() { static bool ok = [] { WSADATA d{}; return WSAStartup(MAKEWORD(2,2), &d) == 0; }(); return ok; }
void closeSocket(AstraSocket s) { if (s != kInvalidSocket) closesocket(s); }
void setSocketTimeout(AstraSocket s, long ms) { DWORD value = static_cast<DWORD>(ms); setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)); }
#else
using AstraSockLen = socklen_t;
bool socketStartup() { return true; }
void closeSocket(AstraSocket s) { if (s != kInvalidSocket) ::close(s); }
void setSocketTimeout(AstraSocket s, long ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#if defined(SO_NOSIGPIPE)
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
}
#endif

bool waitForAccept(AstraSocket socket, long timeoutMs) {
#if defined(_WIN32)
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout{};
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    return select(0, &readable, nullptr, nullptr, &timeout) > 0;
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = POLLIN;
    const int result = poll(&descriptor, 1, static_cast<int>(timeoutMs));
    return result > 0 && (descriptor.revents & (POLLIN | POLLERR | POLLHUP)) != 0;
#endif
}

int waitForReadable(AstraSocket socket, long timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return 0;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const long waitMs = std::max<long>(1, static_cast<long>(remaining.count()));
#if defined(_WIN32)
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket, &readable);
        timeval timeout{};
        timeout.tv_sec = waitMs / 1000;
        timeout.tv_usec = (waitMs % 1000) * 1000;
        const int result = select(0, &readable, nullptr, nullptr, &timeout);
        if (result > 0) return 1;
        if (result == 0) return 0;
        if (WSAGetLastError() == WSAEINTR) continue;
        return -1;
#else
        pollfd descriptor{};
        descriptor.fd = socket;
        descriptor.events = POLLIN;
        const int result = poll(&descriptor, 1, static_cast<int>(waitMs));
        if (result > 0) return (descriptor.revents & (POLLIN | POLLERR | POLLHUP)) != 0 ? 1 : -1;
        if (result == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
#endif
    }
}

int64_t receiveUntil(AstraSocket socket, char* buffer, size_t capacity, std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return -2;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const long timeout = std::max<long>(1, static_cast<long>(remaining.count()));
        const int ready = waitForReadable(socket, timeout);
        if (ready == 0) return -2;
        if (ready < 0) return -1;
#if defined(_WIN32)
        const int received = ::recv(socket, buffer, static_cast<int>(std::min<size_t>(capacity, static_cast<size_t>(std::numeric_limits<int>::max()))), 0);
#else
        const ssize_t received = ::recv(socket, buffer, capacity, 0);
#endif
        if (received < 0) {
#if defined(_WIN32)
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
        }
        return static_cast<int64_t>(received);
    }
}

bool sendAll(AstraSocket socket, const std::string& bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
#if defined(_WIN32)
        int n = ::send(socket, bytes.data() + sent, static_cast<int>(std::min<size_t>(bytes.size() - sent, 1u << 20)), 0);
#else
#if defined(MSG_NOSIGNAL)
        ssize_t n = ::send(socket, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
#else
        ssize_t n = ::send(socket, bytes.data() + sent, bytes.size() - sent, 0);
#endif
#endif
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}
std::string reasonPhrase(long code) {
    switch (code) { case 200:return "OK"; case 201:return "Created"; case 202:return "Accepted"; case 204:return "No Content"; case 301:return "Moved Permanently"; case 302:return "Found"; case 304:return "Not Modified"; case 400:return "Bad Request"; case 401:return "Unauthorized"; case 403:return "Forbidden"; case 404:return "Not Found"; case 405:return "Method Not Allowed"; case 408:return "Request Timeout"; case 413:return "Payload Too Large"; case 415:return "Unsupported Media Type"; case 422:return "Unprocessable Content"; case 429:return "Too Many Requests"; case 431:return "Request Header Fields Too Large"; case 500:return "Internal Server Error"; case 501:return "Not Implemented"; case 503:return "Service Unavailable"; default:return "Response"; }
}
std::string percentDecode(const std::string& input) {
    std::string out; out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '+') { out.push_back(' '); continue; }
        if (input[i] == '%') {
            if (i + 2 >= input.size()) throw std::runtime_error("Malformed URL escape");
            auto hex = [](char c) -> int { if (c >= '0' && c <= '9') return c-'0'; if (c >= 'a' && c <= 'f') return c-'a'+10; if (c >= 'A' && c <= 'F') return c-'A'+10; return -1; };
            int a = hex(input[i+1]), b = hex(input[i+2]); if (a < 0 || b < 0) throw std::runtime_error("Malformed URL escape");
            out.push_back(static_cast<char>((a << 4) | b)); i += 2;
        } else out.push_back(input[i]);
    }
    return out;
}

bool matchRoute(const std::string& route, const std::string& path, std::shared_ptr<Object>& params) {
    params = object();
    std::vector<std::string> a, b;
    auto split = [](const std::string& s, std::vector<std::string>& out) { size_t p = 0; while (p < s.size()) { while (p < s.size() && s[p] == '/') ++p; if (p >= s.size()) break; size_t e = s.find('/', p); if (e == std::string::npos) e = s.size(); out.push_back(s.substr(p, e-p)); p = e; } };
    split(route, a); split(path, b);
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() > 1 && a[i][0] == ':') params->fields[a[i].substr(1)] = Value(percentDecode(b[i]));
        else if (a[i] != b[i]) return false;
    }
    return true;
}

Value invokeHandler(const Value& handler, const Args& args) {
    if (auto p = std::get_if<NativeFunction>(&handler.data)) return (*p)(args);
    if (auto p = std::get_if<std::shared_ptr<Function>>(&handler.data)) { if (*p && (*p)->invoke) return (*p)->invoke(args); }
    if (auto p = std::get_if<std::shared_ptr<Object>>(&handler.data)) { auto it = (*p)->fields.find("__call"); if (it != (*p)->fields.end()) return invokeHandler(it->second, args); }
    throw std::runtime_error("HTTP route handler must be a function");
}

void sendResponse(AstraSocket socket, const ResponseState& state, bool headOnly) {
    std::ostringstream out;
    out << "HTTP/1.1 " << state.status << ' ' << reasonPhrase(state.status) << "\r\n";
    out << "Server: Astra\r\n";
    bool hasType = false;
    for (const auto& h : state.headers) {
        if (lower(h.first) == "server" || lower(h.first) == "content-length" || lower(h.first) == "connection") continue;
        if (lower(h.first) == "content-type") hasType = true;
        out << h.first << ": " << h.second << "\r\n";
    }
    if (!hasType) out << "Content-Type: text/plain; charset=utf-8\r\n";
    out << "Content-Length: " << state.body.size() << "\r\nConnection: close\r\n\r\n";
    std::string headers = out.str(); sendAll(socket, headers);
    if (!headOnly && !state.body.empty()) sendAll(socket, state.body);
}

std::string peerAddress(const sockaddr_storage& peer, AstraSockLen peerLength) {
    char host[NI_MAXHOST]{};
    const int result = getnameinfo(reinterpret_cast<const sockaddr*>(&peer), peerLength, host, sizeof(host), nullptr, 0, NI_NUMERICHOST);
    return result == 0 ? std::string(host) : std::string("unknown");
}

void sendOverload(AstraSocket client, long status, const std::string& message) {
    ResponseState response;
    response.status = status;
    response.headers["Retry-After"] = "1";
    response.body = message;
    sendResponse(client, response, false);
    closeSocket(client);
}

void processConnection(AstraSocket client, const std::shared_ptr<ServerState>& state, size_t maxBodyBytes, size_t maxHeaderBytes, size_t maxResponseBytes, long timeoutMs) {
    setSocketTimeout(client, timeoutMs);
    const auto readDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string raw;
    raw.reserve(4096);
    size_t headerEnd = std::string::npos;
    char buffer[8192];
    for (;;) {
        headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            if (headerEnd > maxHeaderBytes) {
                ResponseState response;
                response.status = 431;
                response.body = "Request headers too large";
                sendResponse(client, response, false);
                return;
            }
            break;
        }
        if (raw.size() > maxHeaderBytes) {
            ResponseState response;
            response.status = 431;
            response.body = "Request headers too large";
            sendResponse(client, response, false);
            return;
        }
        const int64_t n = receiveUntil(client, buffer, sizeof(buffer), readDeadline);
        if (n == -2) {
            ResponseState response;
            response.status = 408;
            response.body = "Request read deadline exceeded";
            sendResponse(client, response, false);
            return;
        }
        if (n <= 0) return;
        raw.append(buffer, static_cast<size_t>(n));
    }
    const std::string headerText = raw.substr(0, headerEnd);
    const size_t firstEnd = headerText.find("\r\n");
    const std::string requestLine = headerText.substr(0, firstEnd);
    std::istringstream first(requestLine); std::string method, target, version, extra;
    if (!(first >> method >> target >> version) || (first >> extra) || (version != "HTTP/1.0" && version != "HTTP/1.1") || target.empty() || target[0] != '/') {
        ResponseState r; r.status=400; r.body="Malformed HTTP request line"; sendResponse(client,r,false); return;
    }
    method = upper(method);
    std::map<std::string,std::string> headers;
    size_t pos = firstEnd == std::string::npos ? headerText.size() : firstEnd + 2;
    while (pos < headerText.size()) {
        size_t e = headerText.find("\r\n",pos); if (e == std::string::npos) e = headerText.size();
        std::string line = headerText.substr(pos,e-pos); pos = e + 2;
        size_t colon = line.find(':'); if (colon == std::string::npos) { ResponseState r; r.status=400; r.body="Malformed HTTP header"; sendResponse(client,r,false); return; }
        std::string key = lower(line.substr(0,colon)); size_t v = line.find_first_not_of(" \t",colon+1); std::string value = v == std::string::npos ? "" : line.substr(v);
        if (!safeHeaderName(key) || !safeHeaderValue(value)) { ResponseState r; r.status=400; r.body="Invalid HTTP header"; sendResponse(client,r,false); return; }
        auto it = headers.find(key); if (it == headers.end()) headers.emplace(key,value); else it->second += ", " + value;
    }
    if (headers.count("transfer-encoding") && lower(headers["transfer-encoding"]) != "identity") { ResponseState r; r.status=501; r.body="Transfer-Encoding is not supported by this preview server"; sendResponse(client,r,false); return; }
    size_t contentLength = 0;
    if (headers.count("content-length")) {
        const std::string& value = headers["content-length"];
        if (value.empty() || !std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isdigit(c);})) { ResponseState r; r.status=400; r.body="Invalid Content-Length"; sendResponse(client,r,false); return; }
        try { unsigned long long n = std::stoull(value); if (n > maxBodyBytes) { ResponseState r; r.status=413; r.body="Request body too large"; sendResponse(client,r,false); return; } contentLength=static_cast<size_t>(n); }
        catch (...) { ResponseState r; r.status=413; r.body="Request body too large"; sendResponse(client,r,false); return; }
    }
    std::string body = raw.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        const int64_t n = receiveUntil(client, buffer, sizeof(buffer), readDeadline);
        if (n <= 0) { ResponseState r; r.status=408; r.body=n == -2 ? "Request read deadline exceeded" : "Incomplete request body"; sendResponse(client,r,false); return; }
        body.append(buffer,static_cast<size_t>(n));
        if (body.size() > maxBodyBytes) { ResponseState r; r.status=413; r.body="Request body too large"; sendResponse(client,r,false); return; }
    }
    if (body.size() > contentLength) body.resize(contentLength);

    const size_t q = target.find('?'); const std::string path = target.substr(0,q);
    auto req = object(); auto reqHeaders = object(); auto reqParams = object(); auto reqQuery = object();
    req->fields["method"] = Value(method); req->fields["url"] = Value(target); req->fields["path"] = Value(path); req->fields["body"] = Value(body);
    for (const auto& h : headers) reqHeaders->fields[h.first] = Value(h.second);
    req->fields["headers"] = Value(reqHeaders); req->fields["params"] = Value(reqParams); req->fields["query"] = Value(reqQuery);
    if (q != std::string::npos) {
        std::string query = target.substr(q+1); size_t p = 0;
        while (p < query.size()) { size_t e=query.find('&',p); if(e==std::string::npos)e=query.size(); size_t eq=query.find('=',p); if(eq==std::string::npos||eq>e)eq=e; try { std::string k=percentDecode(query.substr(p,eq-p)); std::string v=eq<e?percentDecode(query.substr(eq+1,e-eq-1)):""; reqQuery->fields[k]=Value(v); } catch (...) {} p=e+1; }
    }
    auto response = std::make_shared<ResponseState>(); auto res = object(); std::weak_ptr<Object> weakRes = res;
    res->fields["status"] = native([response,weakRes](const Args& a)->Value { need(a,1,"res.status"); auto p=std::get_if<int64_t>(&a[0].data); if(!p||*p<100||*p>599)throw std::runtime_error("HTTP response status must be an integer from 100 to 599"); response->status=static_cast<long>(*p); auto live=weakRes.lock(); return live?Value(live):Value(Null{}); });
    res->fields["setHeader"] = native([response,weakRes](const Args& a)->Value { need(a,2,"res.setHeader"); std::string k=stringArg(a,0,"res.setHeader"),v=valueToString(a[1]); if(lower(k)=="server")throw std::runtime_error("The Server response header is reserved and always set to Astra"); if(!safeHeaderName(k)||!safeHeaderValue(v))throw std::runtime_error("Invalid HTTP response header"); response->headers[k]=v; auto live=weakRes.lock(); return live?Value(live):Value(Null{}); });
    res->fields["send"] = native([response,maxResponseBytes](const Args& a)->Value { if(a.size()>1)throw std::runtime_error("res.send accepts at most one value"); std::string body=a.empty()?"":valueToString(a[0]); if(body.size()>maxResponseBytes)throw std::runtime_error("HTTP response exceeds maxResponseBytes"); response->body=std::move(body); response->ended=true; return Value(Null{}); });
    res->fields["end"] = native([response,maxResponseBytes](const Args& a)->Value { if(a.size()>1)throw std::runtime_error("res.end accepts at most one body"); std::string body=a.empty()?response->body:valueToString(a[0]); if(body.size()>maxResponseBytes)throw std::runtime_error("HTTP response exceeds maxResponseBytes"); response->body=std::move(body); response->ended=true; return Value(Null{}); });
    res->fields["json"] = native([response,maxResponseBytes](const Args& a)->Value { need(a,1,"res.json"); if(a.size()>1)throw std::runtime_error("res.json accepts one value"); std::string body=valueToJson(a[0],false); if(body.size()>maxResponseBytes)throw std::runtime_error("HTTP response exceeds maxResponseBytes"); response->headers["Content-Type"]="application/json; charset=utf-8"; response->body=std::move(body); response->ended=true; return Value(Null{}); });
    req->fields["params"] = Value(reqParams);

    bool handled = false;
    for (const auto& route : state->routes) {
        if (route.method != "ALL" && route.method != method) continue;
        std::shared_ptr<Object> params;
        if (!matchRoute(route.path,path,params)) continue;
        req->fields["params"] = Value(params);
        try {
            std::lock_guard<std::mutex> guard(state->handlerMutex);
            invokeHandler(route.handler,{Value(req),Value(res)});
        } catch (const std::exception&) {
            response->status=500;
            response->body="Internal Server Error";
            response->headers.clear();
            response->ended=true;
        }
        handled=true; break;
    }
    if (!handled) { response->status=404; response->body="Not Found"; response->ended=true; }
    if (!response->ended) { if (response->status == 200) response->status=204; response->ended=true; }
    sendResponse(client,*response,method=="HEAD");
}

Value createServerApp() {
    auto state = std::make_shared<ServerState>();
    auto app = object();
    app->moduleName = "std:server.app";
    auto addRoute = [state](const std::string& method, const Args& a) -> Value {
        if (a.size() != 2) throw std::runtime_error("route registration expects path and handler");
        if (!std::holds_alternative<std::string>(a[0].data)) throw std::runtime_error("route path must be a string");
        const std::string path = std::get<std::string>(a[0].data);
        if (path.empty() || path[0] != '/') throw std::runtime_error("route path must start with '/'");
        if (!std::holds_alternative<std::shared_ptr<Function>>(a[1].data) && !std::holds_alternative<NativeFunction>(a[1].data) && !std::holds_alternative<std::shared_ptr<Object>>(a[1].data)) throw std::runtime_error("route handler must be a function");
        state->routes.push_back({method, path, a[1]});
        return Value(static_cast<int64_t>(state->routes.size()));
    };
    for (const std::string method : {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS", "ALL"}) {
        const std::string name = method == "ALL" ? "all" : lower(method);
        app->fields[name] = native([addRoute, method](const Args& a) { return addRoute(method, a); });
    }
    app->fields["route"] = native([addRoute](const Args& a) {
        if (a.size() != 3) throw std::runtime_error("server.route expects method, path and handler");
        std::string method = upper(stringArg(a, 0, "server.route"));
        if (method != "GET" && method != "POST" && method != "PUT" && method != "PATCH" && method != "DELETE" && method != "HEAD" && method != "OPTIONS" && method != "ALL") throw std::runtime_error("Unsupported HTTP route method");
        return addRoute(method, {a[1], a[2]});
    });
    app->fields["listen"] = native([state](const Args& a) -> Value {
        need(a, 1, "server.listen");
        const long port = integerArg(a[0], "port", 1, 65535, 0);
        const Value options = a.size() > 1 ? a[1] : Value{};
        if (a.size() > 2) throw std::runtime_error("server.listen accepts a port and optional options object");
        const RuntimeConfig& config = RuntimeConfig::instance();
        std::string host = config.stringValue("server", "host", "127.0.0.1");
        Value hostValue = field(options, "host");
        if (!std::holds_alternative<Undefined>(hostValue.data)) {
            if (!std::holds_alternative<std::string>(hostValue.data)) throw std::runtime_error("listen option host must be a string");
            host = std::get<std::string>(hostValue.data);
            if (host.empty()) throw std::runtime_error("listen option host cannot be empty");
        }
        unsigned int hw = std::thread::hardware_concurrency();
        long automaticWorkers = hw == 0 ? 4 : static_cast<long>(std::min(hw, 32u));
        if (automaticWorkers < 2) automaticWorkers = 2;
        const long configuredWorkers = config.integerValue("server", "workers", automaticWorkers, 0, 128);
        const long workersDefault = configuredWorkers == 0 ? automaticWorkers : configuredWorkers;
        const long workersCount = integerArg(field(options, "workers"), "workers", 1, 128, workersDefault);
        const long queueDefault = config.integerValue("server", "queue_capacity", workersCount * 64, 1, 65536);
        const long queueCapacity = integerArg(field(options, "queueCapacity"), "queueCapacity", 1, 65536, queueDefault);
        const long backlogDefault = config.integerValue("server", "backlog", 256, 1, 65535);
        const long backlog = integerArg(field(options, "backlog"), "backlog", 1, 65535, backlogDefault);
        const long maxBodyDefault = static_cast<long>(config.sizeValue("server", "max_request_buffer", 1024u * 1024u, 1, 64u * 1024u * 1024u));
        const long maxBody = integerArg(field(options, "maxRequestBytes"), "maxRequestBytes", 1, 64L * 1024L * 1024L, maxBodyDefault);
        const long maxHeadersDefault = static_cast<long>(config.sizeValue("server", "max_header_buffer", 64u * 1024u, 1024, 1024u * 1024u));
        const long maxHeaders = integerArg(field(options, "maxHeaderBytes"), "maxHeaderBytes", 1024, 1024L * 1024L, maxHeadersDefault);
        const long maxResponseDefault = static_cast<long>(config.sizeValue("server", "max_response_buffer", 4u * 1024u * 1024u, 1, 64u * 1024u * 1024u));
        const long maxResponse = integerArg(field(options, "maxResponseBytes"), "maxResponseBytes", 1, 64L * 1024L * 1024L, maxResponseDefault);
        const long timeoutDefault = config.durationMs("server", "request_timeout", 10000, 100, 120000);
        const long timeout = integerArg(field(options, "timeoutMs"), "timeoutMs", 100, 120000, timeoutDefault);
        const long maxRequests = integerArg(field(options, "maxRequests"), "maxRequests", 0, 2147483647L, 0);
        const long maxConnections = integerArg(field(options, "maxConnections"), "maxConnections", 1, 65536,
            config.integerValue("server", "max_connections", std::min<long>(65536, workersCount + queueCapacity), 1, 65536));
        const long maxConnectionsPerIp = integerArg(field(options, "maxConnectionsPerIp"), "maxConnectionsPerIp", 1, 65536,
            config.integerValue("server", "max_connections_per_ip", 64, 1, 65536));
        const long maxTrackedIps = config.integerValue("server", "max_tracked_ips", 16384, 128, 1048576);
        const bool rateLimitEnabled = config.boolValue("rate_limit", "enabled", true);
        const long rateWindowMs = config.durationMs("rate_limit", "window", 1000, 100, 60000);
        const long maxRequestsTotal = config.integerValue("rate_limit", "max_requests_total", 5000, 1, 10000000);
        const long maxRequestsPerIp = config.integerValue("rate_limit", "max_requests_per_ip", 2000, 1, 1000000);
        if (!socketStartup()) throw std::runtime_error("Socket subsystem initialization failed");
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        addrinfo* result = nullptr;
        const std::string service = std::to_string(port);
        const int gai = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
        if (gai != 0) throw std::runtime_error("Cannot resolve listen host: " + host);
        AstraSocket listener = kInvalidSocket;
        for (addrinfo* ai = result; ai; ai = ai->ai_next) {
            listener = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (listener == kInvalidSocket) continue;
            int yes = 1;
#if defined(_WIN32)
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
            if (::bind(listener, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0 && ::listen(listener, static_cast<int>(backlog)) == 0) break;
            closeSocket(listener);
            listener = kInvalidSocket;
        }
        freeaddrinfo(result);
        if (listener == kInvalidSocket) throw std::runtime_error("Unable to bind HTTP server to " + host + ":" + service);
        std::mutex queueMutex;
        std::condition_variable queueReady;
        std::deque<PendingConnection> pending;
        std::map<std::string, IpLimitState> ipLimits;
        size_t activeConnections = 0;
        uint64_t windowRequests = 0;
        auto globalWindowStart = std::chrono::steady_clock::now();
        bool workersStop = false;
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(workersCount));
        try {
            for (long i = 0; i < workersCount; ++i) {
                workers.emplace_back([&]() {
                    for (;;) {
                        PendingConnection connection{};
                        {
                            std::unique_lock<std::mutex> lock(queueMutex);
                            queueReady.wait(lock, [&]() { return workersStop || !pending.empty(); });
                            if (pending.empty() && workersStop) return;
                            connection = std::move(pending.front());
                            pending.pop_front();
                        }
                        try {
                            processConnection(connection.socket, state, static_cast<size_t>(maxBody), static_cast<size_t>(maxHeaders), static_cast<size_t>(maxResponse), timeout);
                        } catch (...) {
                        }
                        closeSocket(connection.socket);
                        {
                            std::lock_guard<std::mutex> lock(queueMutex);
                            if (activeConnections > 0) --activeConnections;
                            auto ip = ipLimits.find(connection.ip);
                            if (ip != ipLimits.end() && ip->second.active > 0) --ip->second.active;
                        }
                    }
                });
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                workersStop = true;
            }
            queueReady.notify_all();
            for (auto& worker : workers) if (worker.joinable()) worker.join();
            closeSocket(listener);
            throw std::runtime_error("Unable to create the configured HTTP worker pool");
        }
        std::cout << "Astra server listening on http://" << host << ":" << port
                  << " with " << workersCount << " worker(s), queue " << queueCapacity
                  << ", connection cap " << maxConnections << ", per-IP cap " << maxConnectionsPerIp
                  << " and backlog " << backlog << " (Server: Astra)\n";
        if (config.loadedFromFile()) std::cout << "Runtime configuration: " << config.sourcePath() << "\n";
        std::cout << std::flush;
        static volatile std::sig_atomic_t stopRequested = 0;
        stopRequested = 0;
        auto previousInt = std::signal(SIGINT, [](int) { stopRequested = 1; });
        auto previousTerm = std::signal(SIGTERM, [](int) { stopRequested = 1; });
        uint64_t accepted = 0;
        uint64_t rejected = 0;
        while (!stopRequested && (maxRequests == 0 || accepted < static_cast<uint64_t>(maxRequests))) {
            if (!waitForAccept(listener, 250)) continue;
            sockaddr_storage peer{};
#if defined(_WIN32)
            int peerLength = sizeof(peer);
#else
            socklen_t peerLength = sizeof(peer);
#endif
            AstraSocket client = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peerLength);
            if (client == kInvalidSocket) {
#if defined(_WIN32)
                const int error = WSAGetLastError();
                if (error == WSAEINTR && stopRequested) break;
                if (error == WSAEINTR || error == WSAECONNRESET) continue;
#else
                if (errno == EINTR && stopRequested) break;
                if (errno == EINTR || errno == ECONNABORTED) continue;
#endif
                break;
            }
            ++accepted;
            setSocketTimeout(client, timeout);
            const std::string ip = peerAddress(peer, peerLength);
            bool queued = false;
            long rejectionStatus = 503;
            std::string rejectionMessage = "Server is busy";
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                const auto now = std::chrono::steady_clock::now();
                if (now - globalWindowStart >= std::chrono::milliseconds(rateWindowMs)) {
                    globalWindowStart = now;
                    windowRequests = 0;
                }
                auto ipIt = ipLimits.find(ip);
                if (ipIt == ipLimits.end() && ipLimits.size() >= static_cast<size_t>(maxTrackedIps)) {
                    for (auto it = ipLimits.begin(); it != ipLimits.end();) {
                        if (it->second.active == 0 && now - it->second.windowStart >= std::chrono::milliseconds(rateWindowMs)) it = ipLimits.erase(it);
                        else ++it;
                    }
                    ipIt = ipLimits.find(ip);
                }
                if (ipIt == ipLimits.end() && ipLimits.size() < static_cast<size_t>(maxTrackedIps)) {
                    IpLimitState initial;
                    initial.windowStart = now;
                    ipIt = ipLimits.emplace(ip, initial).first;
                }
                if (ipIt == ipLimits.end()) {
                    rejectionStatus = 503;
                    rejectionMessage = "Connection tracking capacity reached";
                } else {
                    if (now - ipIt->second.windowStart >= std::chrono::milliseconds(rateWindowMs)) {
                        ipIt->second.windowStart = now;
                        ipIt->second.requests = 0;
                    }
                    if (rateLimitEnabled && (windowRequests >= static_cast<uint64_t>(maxRequestsTotal) || ipIt->second.requests >= static_cast<uint64_t>(maxRequestsPerIp))) {
                        rejectionStatus = 429;
                        rejectionMessage = "Request rate limit exceeded";
                    } else if (activeConnections >= static_cast<size_t>(maxConnections)) {
                        rejectionStatus = 503;
                        rejectionMessage = "Concurrent connection limit reached";
                    } else if (ipIt->second.active >= static_cast<size_t>(maxConnectionsPerIp)) {
                        rejectionStatus = 429;
                        rejectionMessage = "Per-IP connection limit reached";
                    } else if (workersStop || pending.size() >= static_cast<size_t>(queueCapacity)) {
                        rejectionStatus = 503;
                        rejectionMessage = "Server queue is full";
                    } else {
                        ++windowRequests;
                        ++ipIt->second.requests;
                        ++activeConnections;
                        ++ipIt->second.active;
                        pending.push_back({client, ip});
                        queued = true;
                    }
                }
            }
            if (queued) {
                queueReady.notify_one();
            } else {
                sendOverload(client, rejectionStatus, rejectionMessage);
                ++rejected;
            }
        }
        closeSocket(listener);
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            workersStop = true;
        }
        queueReady.notify_all();
        for (auto& worker : workers) if (worker.joinable()) worker.join();
        std::signal(SIGINT, previousInt);
        std::signal(SIGTERM, previousTerm);
        std::cout << "Astra server stopped after " << accepted << " accepted connection(s); "
                  << rejected << " rejected by configured limits.\n" << std::flush;
        return Value(static_cast<int64_t>(accepted));
    });
    app->fields["routeCount"] = native([state](const Args& a) {
        if (!a.empty()) throw std::runtime_error("routeCount expects no arguments");
        return Value(static_cast<int64_t>(state->routes.size()));
    });
    return Value(app);
}
}

Value createServerBuiltin(const std::string& name) {
    if(name=="std:server.create") return native([](const Args& a){if(a.size()>1)throw std::runtime_error("server.create accepts at most one options object");return createServerApp();});
    return Value{};
}
Value createServerModule(const std::string& name) {
    if(name!="std:server")return Value{};
    auto out=object(); out->moduleName=name;
    out->fields["create"]=createServerBuiltin("std:server.create");
    return Value(out);
}

}
