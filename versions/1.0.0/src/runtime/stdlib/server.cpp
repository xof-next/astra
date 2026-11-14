#include "astra/advanced_stdlib.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
  #include <unistd.h>
  using AstraSocket = int;
  static constexpr AstraSocket kInvalidSocket = -1;
#endif

namespace astra { namespace {
using Args = std::vector<Value>;
struct Route { std::string method; std::string path; Value handler; };
struct ServerState { std::vector<Route> routes; };
struct ResponseState { long status = 200; std::map<std::string, std::string> headers; std::string body; bool ended = false; };

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
bool socketStartup() { static bool ok = [] { WSADATA d{}; return WSAStartup(MAKEWORD(2,2), &d) == 0; }(); return ok; }
void closeSocket(AstraSocket s) { if (s != kInvalidSocket) closesocket(s); }
void setSocketTimeout(AstraSocket s, long ms) { DWORD value = static_cast<DWORD>(ms); setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)); }
#else
bool socketStartup() { return true; }
void closeSocket(AstraSocket s) { if (s != kInvalidSocket) ::close(s); }
void setSocketTimeout(AstraSocket s, long ms) { timeval tv{}; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)); }
#endif

bool sendAll(AstraSocket socket, const std::string& bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
#if defined(_WIN32)
        int n = ::send(socket, bytes.data() + sent, static_cast<int>(std::min<size_t>(bytes.size() - sent, 1u << 20)), 0);
#else
        ssize_t n = ::send(socket, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}
std::string reasonPhrase(long code) {
    switch (code) { case 200:return "OK"; case 201:return "Created"; case 202:return "Accepted"; case 204:return "No Content"; case 301:return "Moved Permanently"; case 302:return "Found"; case 304:return "Not Modified"; case 400:return "Bad Request"; case 401:return "Unauthorized"; case 403:return "Forbidden"; case 404:return "Not Found"; case 405:return "Method Not Allowed"; case 408:return "Request Timeout"; case 413:return "Payload Too Large"; case 415:return "Unsupported Media Type"; case 422:return "Unprocessable Content"; case 429:return "Too Many Requests"; case 500:return "Internal Server Error"; case 501:return "Not Implemented"; case 503:return "Service Unavailable"; default:return "Response"; }
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
    out << "Server: Astra\r\n"; // Reserved: application code cannot disable or override this header.
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

void processConnection(AstraSocket client, const std::shared_ptr<ServerState>& state, size_t maxBodyBytes, long timeoutMs) {
    setSocketTimeout(client, timeoutMs);
    std::string raw; raw.reserve(4096); const size_t maxHeaderBytes = 64u * 1024u;
    size_t headerEnd = std::string::npos;
    char buffer[8192];
    while ((headerEnd = raw.find("\r\n\r\n")) == std::string::npos) {
#if defined(_WIN32)
        int n = ::recv(client, buffer, sizeof(buffer), 0);
#else
        ssize_t n = ::recv(client, buffer, sizeof(buffer), 0);
#endif
        if (n <= 0) return;
        raw.append(buffer, static_cast<size_t>(n));
        if (raw.size() > maxHeaderBytes) { ResponseState r; r.status=431; r.body="Request headers too large"; sendResponse(client,r,false); return; }
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
#if defined(_WIN32)
        int n = ::recv(client,buffer,sizeof(buffer),0);
#else
        ssize_t n = ::recv(client,buffer,sizeof(buffer),0);
#endif
        if (n <= 0) { ResponseState r; r.status=408; r.body="Incomplete request body"; sendResponse(client,r,false); return; }
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
    res->fields["send"] = native([response](const Args& a)->Value { if(a.size()>1)throw std::runtime_error("res.send accepts at most one value"); response->body=a.empty()?"":valueToString(a[0]); response->ended=true; return Value(Null{}); });
    res->fields["end"] = native([response](const Args& a)->Value { if(a.size()>1)throw std::runtime_error("res.end accepts at most one body"); if(!a.empty())response->body=valueToString(a[0]); response->ended=true; return Value(Null{}); });
    res->fields["json"] = native([response](const Args& a)->Value { need(a,1,"res.json"); if(a.size()>1)throw std::runtime_error("res.json accepts one value"); response->headers["Content-Type"]="application/json; charset=utf-8"; response->body=valueToJson(a[0],false); response->ended=true; return Value(Null{}); });
    req->fields["params"] = Value(reqParams);

    bool handled = false;
    for (const auto& route : state->routes) {
        if (route.method != "ALL" && route.method != method) continue;
        std::shared_ptr<Object> params;
        if (!matchRoute(route.path,path,params)) continue;
        req->fields["params"] = Value(params);
        try { invokeHandler(route.handler,{Value(req),Value(res)}); }
        catch (const std::exception&) { response->status=500; response->body="Internal Server Error"; response->headers.clear(); response->ended=true; }
        handled=true; break;
    }
    if (!handled) { response->status=404; response->body="Not Found"; response->ended=true; }
    if (!response->ended) { if (response->status == 200) response->status=204; response->ended=true; }
    sendResponse(client,*response,method=="HEAD");
}

Value createServerApp() {
    auto state = std::make_shared<ServerState>(); auto app = object(); app->moduleName="std:server.app";
    auto addRoute = [state](const std::string& method,const Args& a) -> Value {
        if(a.size()!=2)throw std::runtime_error("route registration expects path and handler");
        if(!std::holds_alternative<std::string>(a[0].data))throw std::runtime_error("route path must be a string");
        const std::string path=std::get<std::string>(a[0].data);
        if(path.empty()||path[0]!='/')throw std::runtime_error("route path must start with '/'");
        if(!std::holds_alternative<std::shared_ptr<Function>>(a[1].data) && !std::holds_alternative<NativeFunction>(a[1].data) && !std::holds_alternative<std::shared_ptr<Object>>(a[1].data))throw std::runtime_error("route handler must be a function");
        state->routes.push_back({method,path,a[1]}); return Value(static_cast<int64_t>(state->routes.size()));
    };
    for(const std::string method : {"GET","POST","PUT","PATCH","DELETE","HEAD","OPTIONS","ALL"}) {
        const std::string name = method=="ALL"?"all":lower(method);
        app->fields[name]=native([addRoute,method](const Args& a){return addRoute(method,a);});
    }
    app->fields["route"] = native([addRoute](const Args& a){ if(a.size()!=3)throw std::runtime_error("server.route expects method, path and handler"); std::string method=upper(stringArg(a,0,"server.route")); if(method!="GET"&&method!="POST"&&method!="PUT"&&method!="PATCH"&&method!="DELETE"&&method!="HEAD"&&method!="OPTIONS"&&method!="ALL")throw std::runtime_error("Unsupported HTTP route method"); return addRoute(method,{a[1],a[2]}); });
    app->fields["listen"] = native([state](const Args& a)->Value {
        need(a,1,"server.listen"); long port=integerArg(a[0],"port",1,65535,0); Value options = a.size()>1?a[1]:Value{};
        if(a.size()>2)throw std::runtime_error("server.listen accepts a port and optional options object");
        std::string host="127.0.0.1"; Value h=field(options,"host"); if(!std::holds_alternative<Undefined>(h.data)){if(!std::holds_alternative<std::string>(h.data))throw std::runtime_error("listen option host must be a string");host=std::get<std::string>(h.data);}
        const long maxBody=integerArg(field(options,"maxRequestBytes"),"maxRequestBytes",1,64L*1024L*1024L,1024L*1024L);
        const long timeout=integerArg(field(options,"timeoutMs"),"timeoutMs",100,120000,10000);
        const long maxRequests=integerArg(field(options,"maxRequests"),"maxRequests",0,1000000,0);
        (void)timeout;
        if(!socketStartup())throw std::runtime_error("Socket subsystem initialization failed");
        addrinfo hints{}; hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM; hints.ai_flags=AI_PASSIVE;
        addrinfo* result=nullptr; const std::string service=std::to_string(port);
        const int gai=getaddrinfo(host.c_str(),service.c_str(),&hints,&result); if(gai!=0)throw std::runtime_error("Cannot resolve listen host: "+host);
        AstraSocket listener=kInvalidSocket;
        for(addrinfo* ai=result;ai;ai=ai->ai_next){listener=::socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);if(listener==kInvalidSocket)continue;int yes=1;
#if defined(_WIN32)
            setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),sizeof(yes));
#else
            setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
#endif
            if(::bind(listener,ai->ai_addr,static_cast<int>(ai->ai_addrlen))==0&&::listen(listener,64)==0) break;
            closeSocket(listener); listener=kInvalidSocket;
        }
        freeaddrinfo(result); if(listener==kInvalidSocket)throw std::runtime_error("Unable to bind HTTP server to "+host+":"+service);
        std::cout<<"Astra server listening on http://"<<host<<":"<<port<<" (Server: Astra)\n"<<std::flush;
        static volatile std::sig_atomic_t stopRequested=0; stopRequested=0;
        auto previous=std::signal(SIGINT,[](int){stopRequested=1;});
        long served=0;
        while(!stopRequested && (maxRequests==0 || served<maxRequests)) {
            sockaddr_storage peer{};
#if defined(_WIN32)
            int peerLen=sizeof(peer);
#else
            socklen_t peerLen=sizeof(peer);
#endif
            AstraSocket client=::accept(listener,reinterpret_cast<sockaddr*>(&peer),&peerLen);
            if(client==kInvalidSocket){
#if defined(_WIN32)
                if(WSAGetLastError()==WSAEINTR)continue;
#else
                if(errno==EINTR)continue;
#endif
                break;
            }
            processConnection(client,state,static_cast<size_t>(maxBody),timeout); closeSocket(client); ++served;
        }
        closeSocket(listener); std::signal(SIGINT,previous); std::cout<<"Astra server stopped after "<<served<<" request(s).\n";
        return Value(static_cast<int64_t>(served));
    });
    app->fields["routeCount"] = native([state](const Args& a){if(!a.empty())throw std::runtime_error("routeCount expects no arguments");return Value(static_cast<int64_t>(state->routes.size()));});
    return Value(app);
}
} // namespace

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

} // namespace astra
