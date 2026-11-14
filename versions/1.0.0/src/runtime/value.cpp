#include "astra/value.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace astra {
bool Environment::define(const std::string& n,const Value& v,bool c){if(values.count(n))return false;values[n]=v;if(c)constants.insert(n);return true;}
Value Environment::get(const std::string& n) const {auto i=values.find(n);if(i!=values.end())return i->second;return parent?parent->get(n):Value{};}
bool Environment::assign(const std::string& n,const Value& v){auto i=values.find(n);if(i!=values.end()){if(constants.count(n))return false;i->second=v;return true;}return parent?parent->assign(n,v):false;}
bool Environment::isConstant(const std::string& n) const {if(values.count(n))return constants.count(n)!=0;return parent?parent->isConstant(n):false;}
std::string valueType(const Value& v){if(std::holds_alternative<Undefined>(v.data))return "undefined";if(std::holds_alternative<Null>(v.data))return "null";if(std::holds_alternative<bool>(v.data))return "bool";if(std::holds_alternative<int64_t>(v.data))return "int";if(std::holds_alternative<double>(v.data))return "float";if(std::holds_alternative<BigInt>(v.data))return "bigint";if(std::holds_alternative<std::string>(v.data))return "string";if(std::holds_alternative<std::shared_ptr<Array>>(v.data))return "array";if(auto p=std::get_if<std::shared_ptr<Object>>(&v.data)){if(!*p)return "object";if((*p)->fields.count("__call"))return "function";return "object";}if(std::holds_alternative<std::shared_ptr<Function>>(v.data)||std::holds_alternative<NativeFunction>(v.data))return "function";return "unknown";}
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
