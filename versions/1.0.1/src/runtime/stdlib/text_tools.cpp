#include "astra/advanced_stdlib.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra { namespace {
using Args = std::vector<Value>;
Value undefinedValue() { return Value{}; }
Value native(NativeFunction f) { return Value(std::move(f)); }
std::shared_ptr<Object> object() { return std::make_shared<Object>(); }
std::shared_ptr<Array> array() { return std::make_shared<Array>(); }
void need(const Args& a, size_t n, const std::string& name) { if (a.size() < n) throw std::runtime_error(name + " expects at least " + std::to_string(n) + " argument(s)"); }
std::string stringArg(const Args& a, size_t i, const std::string& name) { if (i >= a.size() || !std::holds_alternative<std::string>(a[i].data)) throw std::runtime_error(name + " expects string arguments"); return std::get<std::string>(a[i].data); }
int64_t intArg(const Args& a, size_t i, const std::string& name) { if (i >= a.size() || !std::holds_alternative<int64_t>(a[i].data)) throw std::runtime_error(name + " expects integer arguments"); return std::get<int64_t>(a[i].data); }
std::string hexEncode(const std::string& s) { std::ostringstream out; out << std::hex << std::setfill('0'); for (unsigned char c : s) out << std::setw(2) << static_cast<unsigned int>(c); return out.str(); }
std::string hexDecode(const std::string& s) {
    if (s.size() % 2) throw std::runtime_error("Hex input must have an even number of characters");
    std::string out; out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        unsigned int value = 0; std::istringstream in(s.substr(i, 2)); in >> std::hex >> value;
        if (in.fail() || !in.eof()) throw std::runtime_error("Invalid hexadecimal input");
        out.push_back(static_cast<char>(value));
    }
    return out;
}
std::string base64Encode(const std::string& in) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; uint32_t value = 0; int bits = -6;
    for (unsigned char c : in) { value = (value << 8) | static_cast<uint32_t>(c); bits += 8; while (bits >= 0) { out.push_back(table[(value >> bits) & 63]); bits -= 6; } }
    if (bits > -6) out.push_back(table[((value << 8) >> (bits + 8)) & 63]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
std::string base64Decode(const std::string& input) {
    static const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string clean;
    clean.reserve(input.size());
    for (unsigned char c : input) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        clean.push_back(static_cast<char>(c));
    }
    if (clean.size() % 4 == 1) throw std::runtime_error("Invalid base64 length");
    size_t padding = 0;
    if (!clean.empty() && clean.back() == '=') {
        padding = 1;
        if (clean.size() >= 2 && clean[clean.size() - 2] == '=') padding = 2;
    }
    const size_t dataLength = clean.size() - padding;
    if (padding && clean.size() % 4 != 0) throw std::runtime_error("Invalid base64 padding");
    if (padding == 1 && dataLength % 4 != 3) throw std::runtime_error("Invalid base64 padding");
    if (padding == 2 && dataLength % 4 != 2) throw std::runtime_error("Invalid base64 padding");
    std::vector<int> map(256, -1);
    for (size_t i = 0; i < table.size(); ++i) map[static_cast<unsigned char>(table[i])] = static_cast<int>(i);
    std::string out; uint32_t value = 0; int bits = 0;
    for (size_t i = 0; i < dataLength; ++i) {
        const unsigned char c = static_cast<unsigned char>(clean[i]);
        if (map[c] < 0) throw std::runtime_error("Invalid base64 input");
        value = (value << 6) | static_cast<uint32_t>(map[c]); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((value >> bits) & 255)); }
    }
    for (size_t i = dataLength; i < clean.size(); ++i) if (clean[i] != '=') throw std::runtime_error("Invalid base64 padding");
    if (bits > 0 && (value & ((1u << bits) - 1u)) != 0) throw std::runtime_error("Non-canonical base64 trailing bits");
    if (out.size() > 64u * 1024u * 1024u) throw std::runtime_error("Decoded base64 output exceeds 64 MiB");
    return out;
}
std::string urlEncode(const std::string& s) {
    std::ostringstream out; out << std::uppercase << std::hex;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out << static_cast<char>(c);
        else out << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(c);
    }
    return out.str();
}
int hexDigit(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') { if (i + 2 >= s.size()) throw std::runtime_error("Invalid percent escape in query string"); int a = hexDigit(s[i+1]), b = hexDigit(s[i+2]); if (a < 0 || b < 0) throw std::runtime_error("Invalid percent escape in query string"); out.push_back(static_cast<char>((a << 4) | b)); i += 2; }
        else if (s[i] == '+') out.push_back(' '); else out.push_back(s[i]);
    }
    return out;
}
Value parseQuery(const std::string& query) {
    auto result = object();
    size_t start = query.find('?');
    std::string s = start == std::string::npos ? query : query.substr(start + 1);
    const auto hash = s.find('#'); if (hash != std::string::npos) s.resize(hash);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find('&', pos); if (end == std::string::npos) end = s.size();
        std::string pair = s.substr(pos, end - pos); size_t eq = pair.find('=');
        std::string key = urlDecode(pair.substr(0, eq));
        std::string value = eq == std::string::npos ? std::string{} : urlDecode(pair.substr(eq + 1));
        auto it = result->fields.find(key);
        if (it == result->fields.end()) result->fields.emplace(key, Value(value));
        else if (auto existing = std::get_if<std::shared_ptr<Array>>(&it->second.data)) (*existing)->items.emplace_back(value);
        else { auto values = array(); values->items.push_back(it->second); values->items.emplace_back(value); it->second = Value(values); }
        pos = end + 1;
    }
    return Value(result);
}
Value stringifyQuery(const Value& value) {
    auto p = std::get_if<std::shared_ptr<Object>>(&value.data); if (!p) throw std::runtime_error("query.stringify expects an object");
    std::string out; bool first = true;
    auto append = [&](const std::string& key, const Value& v) { if (!first) out += '&'; first = false; out += urlEncode(key) + "=" + urlEncode(valueToString(v)); };
    for (const auto& item : (*p)->fields) {
        if (auto values = std::get_if<std::shared_ptr<Array>>(&item.second.data)) for (const auto& v : (*values)->items) append(item.first, v);
        else if (!std::holds_alternative<Undefined>(item.second.data) && !std::holds_alternative<Null>(item.second.data)) append(item.first, item.second);
    }
    return Value(out);
}
} // namespace

Value createBufferBuiltin(const std::string& name) {
    if (name == "std:buffer.byteLength") return native([](const Args& a) { need(a,1,"buffer.byteLength"); return Value(static_cast<int64_t>(stringArg(a,0,"buffer.byteLength").size())); });
    if (name == "std:buffer.toHex") return native([](const Args& a) { need(a,1,"buffer.toHex"); return Value(hexEncode(stringArg(a,0,"buffer.toHex"))); });
    if (name == "std:buffer.fromHex") return native([](const Args& a) { need(a,1,"buffer.fromHex"); return Value(hexDecode(stringArg(a,0,"buffer.fromHex"))); });
    if (name == "std:buffer.toBase64") return native([](const Args& a) { need(a,1,"buffer.toBase64"); return Value(base64Encode(stringArg(a,0,"buffer.toBase64"))); });
    if (name == "std:buffer.fromBase64") return native([](const Args& a) { need(a,1,"buffer.fromBase64"); return Value(base64Decode(stringArg(a,0,"buffer.fromBase64"))); });
    if (name == "std:buffer.concat") return native([](const Args& a) { need(a,1,"buffer.concat"); auto p = std::get_if<std::shared_ptr<Array>>(&a[0].data); if (!p) throw std::runtime_error("buffer.concat expects an array of strings"); std::string out; for (const auto& v : (*p)->items) { if (!std::holds_alternative<std::string>(v.data)) throw std::runtime_error("buffer.concat array values must be strings"); out += std::get<std::string>(v.data); if (out.size() > 64u*1024u*1024u) throw std::runtime_error("buffer.concat output exceeds 64 MiB"); } return Value(out); });
    if (name == "std:buffer.slice") return native([](const Args& a) { need(a,2,"buffer.slice"); const std::string s = stringArg(a,0,"buffer.slice"); int64_t start = intArg(a,1,"buffer.slice"); int64_t end = a.size()>2 ? intArg(a,2,"buffer.slice") : static_cast<int64_t>(s.size()); const int64_t size = static_cast<int64_t>(s.size()); if (start < 0) start = std::max<int64_t>(0,size+start); else start = std::min(start,size); if (end < 0) end = std::max<int64_t>(0,size+end); else end = std::min(end,size); if (end < start) end = start; return Value(s.substr(static_cast<size_t>(start),static_cast<size_t>(end-start))); });
    return undefinedValue();
}
Value createQueryBuiltin(const std::string& name) {
    if (name == "std:query.parse") return native([](const Args& a) { need(a,1,"query.parse"); return parseQuery(stringArg(a,0,"query.parse")); });
    if (name == "std:query.stringify") return native([](const Args& a) { need(a,1,"query.stringify"); return stringifyQuery(a[0]); });
    if (name == "std:query.get") return native([](const Args& a) { need(a,2,"query.get"); Value obj = parseQuery(stringArg(a,0,"query.get")); auto p = std::get<std::shared_ptr<Object>>(obj.data); auto it = p->fields.find(stringArg(a,1,"query.get")); return it == p->fields.end() ? Value{} : it->second; });
    if (name == "std:query.append") return native([](const Args& a) { need(a,3,"query.append"); std::string q = stringArg(a,0,"query.append"); std::string key = stringArg(a,1,"query.append"); std::string value = valueToString(a[2]); const size_t fragment=q.find('#'); std::string suffix=fragment==std::string::npos?std::string{}:q.substr(fragment); if(fragment!=std::string::npos)q.resize(fragment); const char separator=q.find('?')==std::string::npos?'?':((!q.empty()&&(q.back()=='?'||q.back()=='&'))?'\0':'&'); if(separator!='\0')q.push_back(separator); q += urlEncode(key) + "=" + urlEncode(value) + suffix; return Value(q); });
    return undefinedValue();
}
Value createValidationBuiltin(const std::string& name) {
    if (name == "std:validate.isEmail") return native([](const Args& a) { need(a,1,"validate.isEmail"); const auto s=stringArg(a,0,"validate.isEmail"); static const std::regex r(R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$)"); return Value(std::regex_match(s,r)); });
    if (name == "std:validate.isUUID") return native([](const Args& a) { need(a,1,"validate.isUUID"); const auto s=stringArg(a,0,"validate.isUUID"); static const std::regex r(R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$)"); return Value(std::regex_match(s,r)); });
    if (name == "std:validate.isHex") return native([](const Args& a) { need(a,1,"validate.isHex"); const auto s=stringArg(a,0,"validate.isHex"); return Value(!s.empty() && std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isxdigit(c)!=0;})); });
    if (name == "std:validate.isIPv4") return native([](const Args& a) { need(a,1,"validate.isIPv4"); const auto s=stringArg(a,0,"validate.isIPv4"); size_t start=0; for(int partIndex=0;partIndex<4;++partIndex){size_t end=s.find('.',start); if((partIndex<3&&end==std::string::npos)||(partIndex==3&&end!=std::string::npos))return Value(false); if(end==std::string::npos)end=s.size(); const std::string part=s.substr(start,end-start); if(part.empty()||part.size()>3||!std::all_of(part.begin(),part.end(),[](unsigned char c){return std::isdigit(c)!=0;}))return Value(false); if(part.size()>1&&part[0]=='0')return Value(false); if(std::stoi(part)>255)return Value(false); start=end+1;} return Value(start==s.size()+1); });
    if (name == "std:validate.isURL") return native([](const Args& a) { need(a,1,"validate.isURL"); const auto s=stringArg(a,0,"validate.isURL"); const bool scheme=s.rfind("http://",0)==0||s.rfind("https://",0)==0; if(!scheme) return Value(false); const auto start=s.find("://")+3; const auto end=s.find_first_of("/?#",start); const auto host=s.substr(start,end==std::string::npos?std::string::npos:end-start); return Value(!host.empty()&&host.find(' ')==std::string::npos&&host.find('@')==std::string::npos); });
    if (name == "std:validate.isEmpty") return native([](const Args& a) { need(a,1,"validate.isEmpty"); if(std::holds_alternative<Undefined>(a[0].data)||std::holds_alternative<Null>(a[0].data))return Value(true); if(auto s=std::get_if<std::string>(&a[0].data))return Value(std::all_of(s->begin(),s->end(),[](unsigned char c){return std::isspace(c)!=0;})); if(auto v=std::get_if<std::shared_ptr<Array>>(&a[0].data))return Value((*v)->items.empty()); if(auto o=std::get_if<std::shared_ptr<Object>>(&a[0].data))return Value((*o)->fields.empty()); return Value(false); });
    return undefinedValue();
}
} // namespace astra
