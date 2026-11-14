#include "astra/value.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <condition_variable>
#include <thread>

namespace astra {
namespace {
size_t activeAsyncTasks=0;
std::mutex asyncTasksMutex;
std::condition_variable asyncTasksCondition;
constexpr size_t maxAsyncTasks = 16;
}
bool Environment::define(const std::string& n,const Value& v,bool c){std::lock_guard<std::mutex>lock(mutex);if(values.count(n)||imports.count(n))return false;values[n]=v;if(c)constants.insert(n);return true;}
bool Environment::defineImport(const std::string& n,const std::shared_ptr<Environment>& source,const std::string& name){std::lock_guard<std::mutex>lock(mutex);if(imports.count(n))return false;auto existing=values.find(n);if(existing!=values.end()){if(constants.count(n)||!std::holds_alternative<Undefined>(existing->second.data))return false;values.erase(existing);}imports.emplace(n,std::make_pair(source,name));return true;}
bool Environment::hasOwn(const std::string& n) const {std::lock_guard<std::mutex>lock(mutex);return values.count(n)||imports.count(n);}
bool Environment::hasBinding(const std::string& n) const {std::shared_ptr<Environment> next;{std::lock_guard<std::mutex>lock(mutex);if(values.count(n)||imports.count(n))return true;next=parent;}return next?next->hasBinding(n):false;}
Value Environment::get(const std::string& n) const {std::shared_ptr<Environment> next;std::shared_ptr<Environment> imported;std::string importedName;{std::lock_guard<std::mutex>lock(mutex);auto i=values.find(n);if(i!=values.end())return i->second;auto alias=imports.find(n);if(alias!=imports.end()){imported=alias->second.first;importedName=alias->second.second;}else next=parent;}if(imported)return imported->get(importedName);return next?next->get(n):Value{};}
bool Environment::assign(const std::string& n,const Value& v){std::shared_ptr<Environment> next;{std::lock_guard<std::mutex>lock(mutex);if(imports.count(n))return false;auto i=values.find(n);if(i!=values.end()){if(constants.count(n))return false;i->second=v;return true;}next=parent;}return next?next->assign(n,v):false;}
bool Environment::isConstant(const std::string& n) const {std::shared_ptr<Environment> next;{std::lock_guard<std::mutex>lock(mutex);if(imports.count(n))return true;if(values.count(n))return constants.count(n)!=0;next=parent;}return next?next->isConstant(n):false;}
Value makeFuture(std::function<Value()> task){
    auto promise=std::make_shared<std::promise<Value>>();
    auto result=promise->get_future().share();
    bool runInline=false;
    {
        std::lock_guard<std::mutex> lock(asyncTasksMutex);
        if(activeAsyncTasks>=maxAsyncTasks) runInline=true;
        else ++activeAsyncTasks;
    }
    if(runInline){
        try{promise->set_value(task());}catch(...){try{promise->set_exception(std::current_exception());}catch(...){}}
    }else{
        auto operation=std::make_shared<std::function<Value()>>(std::move(task));
        auto execute=[promise,operation](){
            try{promise->set_value((*operation)());}catch(...){try{promise->set_exception(std::current_exception());}catch(...){}}
            {
                std::lock_guard<std::mutex> lock(asyncTasksMutex);
                --activeAsyncTasks;
            }
            asyncTasksCondition.notify_all();
        };
        try{
            std::thread worker(execute);
            try{worker.detach();}catch(...){if(worker.joinable())worker.join();}
        }catch(...){execute();}
    }
    return Value(std::make_shared<Future>(std::move(result)));
}
Value resolveAwait(const Value& value){
    Value current=value;
    for(size_t depth=0;depth<64;++depth){
        auto future=std::get_if<std::shared_ptr<Future>>(&current.data);
        if(!future||!*future)return current;
        if(!(*future)->result.valid())throw std::runtime_error("Cannot await an invalid asynchronous task");
        current=(*future)->result.get();
    }
    throw std::runtime_error("Asynchronous result nesting limit exceeded");
}
void waitForAsyncTasks(){std::unique_lock<std::mutex>lock(asyncTasksMutex);asyncTasksCondition.wait(lock,[]{return activeAsyncTasks==0;});}
std::string valueType(const Value& v){if(std::holds_alternative<Undefined>(v.data))return "undefined";if(std::holds_alternative<Null>(v.data))return "null";if(std::holds_alternative<bool>(v.data))return "bool";if(std::holds_alternative<int64_t>(v.data))return "int";if(std::holds_alternative<double>(v.data))return "float";if(std::holds_alternative<BigInt>(v.data))return "bigint";if(std::holds_alternative<std::string>(v.data))return "string";if(std::holds_alternative<std::shared_ptr<Array>>(v.data))return "array";if(auto p=std::get_if<std::shared_ptr<Object>>(&v.data)){if(!*p)return "object";if((*p)->fields.count("__call"))return "function";return "object";}if(std::holds_alternative<std::shared_ptr<Function>>(v.data)||std::holds_alternative<NativeFunction>(v.data))return "function";if(std::holds_alternative<std::shared_ptr<Future>>(v.data))return "promise";return "unknown";}
bool isTruthy(const Value& v){if(std::holds_alternative<Undefined>(v.data)||std::holds_alternative<Null>(v.data))return false;if(auto p=std::get_if<bool>(&v.data))return *p;if(auto p=std::get_if<int64_t>(&v.data))return *p!=0;if(auto p=std::get_if<BigInt>(&v.data))return p->digits!="0";if(auto p=std::get_if<double>(&v.data))return *p!=0&&!std::isnan(*p);if(auto p=std::get_if<std::string>(&v.data))return !p->empty();return true;}
namespace {
std::string valueToStringImpl(const Value& value, size_t depth) {
    if (depth > 128) throw std::runtime_error("String conversion maximum nesting depth exceeded (128)");
    if (std::holds_alternative<Undefined>(value.data)) return "undefined";
    if (std::holds_alternative<Null>(value.data)) return "null";
    if (auto p = std::get_if<bool>(&value.data)) return *p ? "true" : "false";
    if (auto p = std::get_if<int64_t>(&value.data)) return std::to_string(*p);
    if (auto p = std::get_if<double>(&value.data)) { std::ostringstream out; out << std::setprecision(15) << *p; return out.str(); }
    if (auto p = std::get_if<BigInt>(&value.data)) return p->toString();
    if (auto p = std::get_if<std::string>(&value.data)) return *p;
    if (auto p = std::get_if<std::shared_ptr<Array>>(&value.data)) {
        if (!*p) return "";
        std::string out;
        for (size_t i = 0; i < (*p)->items.size(); ++i) {
            if (i) out += ",";
            out += valueToStringImpl((*p)->items[i], depth + 1);
            if (out.size() > 64u * 1024u * 1024u) throw std::runtime_error("String conversion exceeds 64 MiB safety limit");
        }
        return out;
    }
    if (std::holds_alternative<std::shared_ptr<Object>>(value.data)) return "[object Object]";
    if (std::holds_alternative<std::shared_ptr<Future>>(value.data)) return "[Promise]";
    return "[function]";
}
}
std::string valueToString(const Value& value) { return valueToStringImpl(value, 0); }
namespace {
constexpr size_t kJsonMaxOutput = 64u * 1024u * 1024u;
constexpr int kJsonMaxDepth = 128;
std::string escapedJsonString(const std::string& value) {
    if (value.size() > kJsonMaxOutput) throw std::runtime_error("JSON string exceeds 64 MiB safety limit");
    static const char hex[] = "0123456789abcdef";
    std::string out; out.reserve(value.size() + 2); out.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 0x0f]); }
                else out.push_back(static_cast<char>(c));
        }
        if (out.size() > kJsonMaxOutput) throw std::runtime_error("JSON output exceeds 64 MiB safety limit");
    }
    out.push_back('"');
    return out;
}
void checkJsonSize(const std::string& value) {
    if (value.size() > kJsonMaxOutput) throw std::runtime_error("JSON output exceeds 64 MiB safety limit");
}
}
std::string valueToJson(const Value& v,bool pretty,int depth){
    if (depth > kJsonMaxDepth) throw std::runtime_error("JSON serialization maximum nesting depth exceeded (128)");
    if(std::holds_alternative<Undefined>(v.data)||std::holds_alternative<Null>(v.data))return "null";
    if(auto p=std::get_if<std::string>(&v.data))return escapedJsonString(*p);
    if(auto p=std::get_if<bool>(&v.data))return *p?"true":"false";
    if(auto p=std::get_if<int64_t>(&v.data))return std::to_string(*p);
    if(auto p=std::get_if<double>(&v.data)){if(!std::isfinite(*p))return "null";return valueToString(v);}
    if(std::holds_alternative<BigInt>(v.data))return escapedJsonString(valueToString(v));
    const std::string pad=pretty?std::string((depth+1)*2,' '):"";
    const std::string endpad=pretty?std::string(depth*2,' '):"";
    if(auto p=std::get_if<std::shared_ptr<Array>>(&v.data)){
        if(!*p)return "[]";
        std::string out="[";
        for(size_t i=0;i<(*p)->items.size();++i){
            if(i)out+=",";
            if(pretty)out+="\n"+pad;
            out+=valueToJson((*p)->items[i],pretty,depth+1);
            checkJsonSize(out);
        }
        if(pretty&&!(*p)->items.empty())out+="\n"+endpad;
        out+="]";checkJsonSize(out);return out;
    }
    if(auto p=std::get_if<std::shared_ptr<Object>>(&v.data)){
        if(!*p)return "{}";
        std::string out="{";size_t index=0;
        for(const auto& kv:(*p)->fields){
            if(index++)out+=",";
            if(pretty)out+="\n"+pad;
            out+=escapedJsonString(kv.first)+(pretty?": ":":");
            out+=valueToJson(kv.second,pretty,depth+1);
            checkJsonSize(out);
        }
        if(pretty&&!(*p)->fields.empty())out+="\n"+endpad;
        out+="}";checkJsonSize(out);return out;
    }
    return "null";
}
}
