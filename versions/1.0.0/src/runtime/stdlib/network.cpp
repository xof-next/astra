#include "astra/advanced_stdlib.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h>
  using AstraSocket = SOCKET;
  static constexpr AstraSocket kInvalidSocket = INVALID_SOCKET;
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netdb.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using AstraSocket = int;
  static constexpr AstraSocket kInvalidSocket = -1;
#endif
namespace astra { namespace {
using Args=std::vector<Value>;
struct TcpState { std::mutex mutex; AstraSocket socket=kInvalidSocket; bool closed=false; };
Value native(NativeFunction f){return Value(std::move(f));}
std::shared_ptr<Object> object(){return std::make_shared<Object>();}
void need(const Args&a,size_t n,const std::string&name){if(a.size()<n)throw std::runtime_error(name+" expects at least "+std::to_string(n)+" argument(s)");}
std::string stringArg(const Args&a,size_t i,const std::string&name){if(i>=a.size()||!std::holds_alternative<std::string>(a[i].data))throw std::runtime_error(name+" expects a string");return std::get<std::string>(a[i].data);}
long integerArg(const Value&v,const std::string&name,long min,long max,long fallback){if(std::holds_alternative<Undefined>(v.data))return fallback;auto p=std::get_if<int64_t>(&v.data);if(!p||*p<min||*p>max)throw std::runtime_error(name+" must be an integer from "+std::to_string(min)+" to "+std::to_string(max));return static_cast<long>(*p);}
Value get(const Value&v,const std::string&key){auto p=std::get_if<std::shared_ptr<Object>>(&v.data);if(!p||!*p)return Value{};auto i=(*p)->fields.find(key);return i==(*p)->fields.end()?Value{}:i->second;}
#if defined(_WIN32)
bool startup(){static bool ok=[](){WSADATA d{};return WSAStartup(MAKEWORD(2,2),&d)==0;}();return ok;}
void closeSocket(AstraSocket s){if(s!=kInvalidSocket)closesocket(s);}
#else
bool startup(){return true;}
void closeSocket(AstraSocket s){if(s!=kInvalidSocket)::close(s);}
#endif
std::string familyName(int f){return f==AF_INET?"IPv4":f==AF_INET6?"IPv6":"unknown";}
Value addressList(const std::string&host){
    if(!startup())throw std::runtime_error("Socket subsystem initialization failed");
    addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;addrinfo*result=nullptr;
    const int rc=getaddrinfo(host.c_str(),nullptr,&hints,&result);if(rc!=0)throw std::runtime_error("DNS lookup failed for `"+host+"`");
    auto out=std::make_shared<Array>();std::vector<std::string>seen;
    for(addrinfo*p=result;p&&out->items.size()<64;p=p->ai_next){char buffer[INET6_ADDRSTRLEN]{};const void*src=nullptr;if(p->ai_family==AF_INET)src=&reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr;else if(p->ai_family==AF_INET6)src=&reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr;else continue;if(!inet_ntop(p->ai_family,src,buffer,sizeof(buffer)))continue;std::string ip(buffer);if(std::find(seen.begin(),seen.end(),ip)!=seen.end())continue;seen.push_back(ip);auto row=object();row->fields["address"]=Value(ip);row->fields["family"]=Value(familyName(p->ai_family));row->fields["version"]=Value(static_cast<int64_t>(p->ai_family==AF_INET?4:6));out->items.emplace_back(row);}
    freeaddrinfo(result);if(out->items.empty())throw std::runtime_error("DNS lookup returned no IP addresses for `"+host+"`");return Value(out);
}
void setTimeout(AstraSocket s,long ms){
#if defined(_WIN32)
    DWORD v=static_cast<DWORD>(ms);setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&v),sizeof(v));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&v),sizeof(v));
#else
    timeval tv{};tv.tv_sec=ms/1000;tv.tv_usec=(ms%1000)*1000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
#endif
}
AstraSocket connectTcp(const std::string&host,long port,long timeoutMs){
    if(!startup())throw std::runtime_error("Socket subsystem initialization failed");
    addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;addrinfo*result=nullptr;std::string service=std::to_string(port);
    int gai=getaddrinfo(host.c_str(),service.c_str(),&hints,&result);if(gai!=0)throw std::runtime_error("Cannot resolve TCP host `"+host+"`");
    AstraSocket connected=kInvalidSocket;
    for(addrinfo*p=result;p;p=p->ai_next){AstraSocket s=::socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(s==kInvalidSocket)continue;
#if defined(_WIN32)
        u_long nonblock=1;ioctlsocket(s,FIONBIO,&nonblock);int rc=::connect(s,p->ai_addr,static_cast<int>(p->ai_addrlen));if(rc==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK){fd_set writes;FD_ZERO(&writes);FD_SET(s,&writes);timeval tv{timeoutMs/1000,(timeoutMs%1000)*1000};rc=select(0,nullptr,&writes,nullptr,&tv);if(rc>0){int err=0;int len=sizeof(err);getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&err),&len);rc=err==0?0:SOCKET_ERROR;}else rc=SOCKET_ERROR;}nonblock=0;ioctlsocket(s,FIONBIO,&nonblock);
#else
        int flags=fcntl(s,F_GETFL,0);fcntl(s,F_SETFL,flags|O_NONBLOCK);int rc=::connect(s,p->ai_addr,static_cast<socklen_t>(p->ai_addrlen));if(rc<0&&errno==EINPROGRESS){fd_set writes;FD_ZERO(&writes);FD_SET(s,&writes);timeval tv{timeoutMs/1000,(timeoutMs%1000)*1000};rc=select(s+1,nullptr,&writes,nullptr,&tv);if(rc>0){int err=0;socklen_t len=sizeof(err);getsockopt(s,SOL_SOCKET,SO_ERROR,&err,&len);rc=err==0?0:-1;}else rc=-1;}if(flags>=0)fcntl(s,F_SETFL,flags);
#endif
        if(rc==0){connected=s;break;}closeSocket(s);
    }
    freeaddrinfo(result);if(connected==kInvalidSocket)throw std::runtime_error("TCP connection to "+host+":"+service+" failed or timed out");setTimeout(connected,timeoutMs);return connected;
}
void closeState(const std::shared_ptr<TcpState>&state){std::lock_guard<std::mutex>lock(state->mutex);if(!state->closed){closeSocket(state->socket);state->socket=kInvalidSocket;state->closed=true;}}
Value createTcpClient(AstraSocket socket){auto state=std::make_shared<TcpState>();state->socket=socket;auto out=object();out->moduleName="std:tcp.socket";
    out->fields["write"]=native([state](const Args&a)->Value{need(a,1,"tcp.write");if(a.size()!=1||!std::holds_alternative<std::string>(a[0].data))throw std::runtime_error("tcp.write expects exactly one string or byte-string");const std::string&data=std::get<std::string>(a[0].data);std::lock_guard<std::mutex>lock(state->mutex);if(state->closed)throw std::runtime_error("TCP socket is closed");size_t sent=0;while(sent<data.size()){
#if defined(_WIN32)
        int n=::send(state->socket,data.data()+sent,static_cast<int>(std::min<size_t>(data.size()-sent,1u<<20)),0);
#else
        ssize_t n=::send(state->socket,data.data()+sent,data.size()-sent,MSG_NOSIGNAL);
#endif
        if(n<=0)throw std::runtime_error("TCP write failed");
        sent+=static_cast<size_t>(n);
    }return Value(static_cast<int64_t>(sent));});
    out->fields["read"]=native([state](const Args&a)->Value{long max=4096;if(!a.empty())max=integerArg(a[0],"tcp.read maxBytes",1,1024L*1024L,4096);if(a.size()>1)throw std::runtime_error("tcp.read accepts at most maxBytes");std::string data(static_cast<size_t>(max),'\0');std::lock_guard<std::mutex>lock(state->mutex);if(state->closed)throw std::runtime_error("TCP socket is closed");
#if defined(_WIN32)
        int n=::recv(state->socket,data.data(),static_cast<int>(data.size()),0);
#else
        ssize_t n=::recv(state->socket,data.data(),data.size(),0);
#endif
        if(n<0)throw std::runtime_error("TCP read failed or timed out");
        if(n==0){closeSocket(state->socket);state->socket=kInvalidSocket;state->closed=true;return Value(Null{});}
        data.resize(static_cast<size_t>(n));return Value(data);});
    out->fields["close"]=native([state](const Args&a){if(!a.empty())throw std::runtime_error("tcp.close expects no arguments");bool wasOpen=!state->closed;closeState(state);return Value(wasOpen);});
    out->fields["isOpen"]=native([state](const Args&a){if(!a.empty())throw std::runtime_error("tcp.isOpen expects no arguments");std::lock_guard<std::mutex>lock(state->mutex);return Value(!state->closed);});
    return Value(out);}
Value dnsBuiltin(const std::string&name){
    if(name=="std:dns.resolve"||name=="std:dns.lookup")return native([name](const Args&a){need(a,1,name);if(a.size()!=1)throw std::runtime_error(name+" expects one hostname");const std::string host=stringArg(a,0,name);Value list=addressList(host);if(name=="std:dns.resolve")return list;auto arr=std::get<std::shared_ptr<Array>>(list.data);return arr->items.front();});
    if(name=="std:dns.isIP")return native([](const Args&a){need(a,1,"dns.isIP");std::string ip=stringArg(a,0,"dns.isIP");in_addr v4{};in6_addr v6{};if(inet_pton(AF_INET,ip.c_str(),&v4)==1)return Value(int64_t(4));if(inet_pton(AF_INET6,ip.c_str(),&v6)==1)return Value(int64_t(6));return Value(int64_t(0));});
    if(name=="std:dns.reverse")return native([](const Args&a){need(a,1,"dns.reverse");std::string ip=stringArg(a,0,"dns.reverse");sockaddr_storage address{};socklen_t len=0;auto*v4=reinterpret_cast<sockaddr_in*>(&address);auto*v6=reinterpret_cast<sockaddr_in6*>(&address);if(inet_pton(AF_INET,ip.c_str(),&v4->sin_addr)==1){v4->sin_family=AF_INET;len=sizeof(sockaddr_in);}else if(inet_pton(AF_INET6,ip.c_str(),&v6->sin6_addr)==1){v6->sin6_family=AF_INET6;len=sizeof(sockaddr_in6);}else throw std::runtime_error("dns.reverse expects a valid IP address");char host[NI_MAXHOST]{};int rc=getnameinfo(reinterpret_cast<sockaddr*>(&address),len,host,sizeof(host),nullptr,0,NI_NAMEREQD);if(rc!=0)throw std::runtime_error("Reverse DNS lookup failed for `"+ip+"`");return Value(std::string(host));});
    return Value{};
}
} // namespace
Value createNetworkBuiltin(const std::string&name){
    if(name.rfind("std:dns.",0)==0)return dnsBuiltin(name);
    if(name=="std:tcp.connect")return native([](const Args&a){need(a,2,"tcp.connect");if(a.size()>3)throw std::runtime_error("tcp.connect expects host, port and optional options");std::string host=stringArg(a,0,"tcp.connect");long port=integerArg(a[1],"tcp.connect port",1,65535,0);Value options=a.size()>2?a[2]:Value{};long timeout=integerArg(get(options,"timeoutMs"),"tcp.connect timeoutMs",100,120000,5000);return createTcpClient(connectTcp(host,port,timeout));});
    return Value{};
}
Value createNetworkModule(const std::string&name){
    if(name!="std:dns"&&name!="std:tcp")return Value{};
    auto out=object();out->moduleName=name;
    if(name=="std:dns"){for(const char*e:{"lookup","resolve","reverse","isIP"})out->fields[e]=dnsBuiltin(std::string("std:dns.")+e);}
    else out->fields["connect"]=createNetworkBuiltin("std:tcp.connect");
    return Value(out);
}
} // namespace astra
