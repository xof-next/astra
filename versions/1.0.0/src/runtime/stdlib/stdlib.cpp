#include "astra/stdlib.hpp"
#include "astra/advanced_stdlib.hpp"
#include "astra/crypto.hpp"
#include "astra/modules.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <ctime>
#include <random>
#include <cctype>
#include <system_error>

namespace astra { namespace {
using Args=std::vector<Value>;
Value undef(){return Value{};}
Value native(NativeFunction f){return Value(std::move(f));}
std::shared_ptr<Object> object(){return std::make_shared<Object>();}
std::shared_ptr<Array> array(){return std::make_shared<Array>();}
void need(const Args&a,size_t n,const std::string& fn){if(a.size()<n)throw std::runtime_error(fn+" expects at least "+std::to_string(n)+" argument(s), got "+std::to_string(a.size()));}
double number(const Value&v){if(auto p=std::get_if<int64_t>(&v.data))return static_cast<double>(*p);if(auto p=std::get_if<double>(&v.data))return *p;if(auto p=std::get_if<BigInt>(&v.data)){try{return std::stod(p->toString());}catch(...){throw std::runtime_error("BigInt is outside Number range");}}if(auto p=std::get_if<bool>(&v.data))return *p?1:0;if(auto p=std::get_if<std::string>(&v.data)){size_t n=0;double x=std::stod(*p,&n);if(n!=p->size())throw std::runtime_error("Cannot convert string to number");return x;}throw std::runtime_error("Expected a numeric value, received "+valueType(v));}
int64_t integer(const Value& v) {
    if (auto p = std::get_if<int64_t>(&v.data)) return *p;
    if (auto p = std::get_if<BigInt>(&v.data)) {
        try { return static_cast<int64_t>(std::stoll(p->toString())); }
        catch (...) { throw std::runtime_error("Integer conversion is out of range"); }
    }
    if (auto p = std::get_if<std::string>(&v.data)) {
        const std::string text = *p;
        try {
            size_t consumed = 0;
            const long long parsed = std::stoll(text, &consumed, 10);
            if (consumed == text.size()) return static_cast<int64_t>(parsed);
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Integer conversion is out of range");
        } catch (const std::invalid_argument&) {
            // Fall through to the normal numeric conversion diagnostic.
        }
    }
    const double d = number(v);
    const long double wide = static_cast<long double>(d);
    const long double minValue = static_cast<long double>(INT64_MIN);
    const long double maxExclusive = -minValue;
    if (!std::isfinite(d) || wide < minValue || wide >= maxExclusive)
        throw std::runtime_error("Integer conversion is out of range");
    return static_cast<int64_t>(d);
}
std::string stringArg(const Args&a,size_t i){if(i>=a.size())throw std::runtime_error("Missing string argument");return valueToString(a[i]);}
std::shared_ptr<Array> asArray(const Value&v){if(auto p=std::get_if<std::shared_ptr<Array>>(&v.data))return *p;throw std::runtime_error("Expected array, received "+valueType(v));}
std::string upper(std::string s){for(char&c:s)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));return s;}
std::string lower(std::string s){for(char&c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return "";auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
std::string base64Encode(const std::string& in){static const char*t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;uint32_t val=0;int bits=-6;for(unsigned char c:in){val=(val<<8)|static_cast<uint32_t>(c);bits+=8;while(bits>=0){out.push_back(t[(val>>bits)&63]);bits-=6;}}if(bits>-6)out.push_back(t[((val<<8)>>(bits+8))&63]);while(out.size()%4)out.push_back('=');return out;}
std::string base64Decode(const std::string& input) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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
    for (size_t i = 0; i < alphabet.size(); ++i) map[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    std::string out;
    uint32_t bitsValue = 0;
    int bits = 0;
    for (size_t i = 0; i < dataLength; ++i) {
        const unsigned char c = static_cast<unsigned char>(clean[i]);
        if (map[c] < 0) throw std::runtime_error("Invalid base64 character");
        bitsValue = (bitsValue << 6) | static_cast<uint32_t>(map[c]);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(static_cast<char>((bitsValue >> bits) & 0xff)); }
    }
    for (size_t i = dataLength; i < clean.size(); ++i) if (clean[i] != '=') throw std::runtime_error("Invalid base64 padding");
    if (bits > 0 && (bitsValue & ((1u << bits) - 1u)) != 0) throw std::runtime_error("Non-canonical base64 trailing bits");
    if (out.size() > 64u * 1024u * 1024u) throw std::runtime_error("Decoded base64 output exceeds 64 MiB");
    return out;
}
std::string hexEncode(const std::string&s){std::ostringstream o;o<<std::hex<<std::setfill('0');for(unsigned char c:s)o<<std::setw(2)<<static_cast<int>(c);return o.str();}
std::string hexDecode(const std::string& s) {
    if (s.size() % 2) throw std::runtime_error("Hex input must have an even number of characters");
    if (s.size() / 2 > 64u * 1024u * 1024u) throw std::runtime_error("Decoded hex output exceeds 64 MiB");
    auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    std::string out; out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) { int a = digit(s[i]), b = digit(s[i + 1]); if (a < 0 || b < 0) throw std::runtime_error("Invalid hex input"); out.push_back(static_cast<char>((a << 4) | b)); }
    return out;
}
std::string urlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out; out.reserve(s.size());
    for (unsigned char c : s) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}
std::string urlDecode(const std::string& s) {
    auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    std::string out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%') {
            if (i + 2 >= s.size()) throw std::runtime_error("Invalid URL escape sequence");
            const int a = digit(s[i + 1]), b = digit(s[i + 2]);
            if (a < 0 || b < 0) throw std::runtime_error("Invalid URL escape sequence");
            out.push_back(static_cast<char>((a << 4) | b)); i += 2;
        } else out.push_back(s[i] == '+' ? ' ' : s[i]);
    }
    return out;
}
class JsonReader {
    const std::string& source;
    size_t pos = 0;
    static constexpr size_t kMaxDepth = 128;
    static constexpr size_t kMaxInput = 64u * 1024u * 1024u;
    void fail(const std::string& message) const {
        throw std::runtime_error("JSON parse error at byte " + std::to_string(pos) + ": " + message);
    }
    void whitespace() {
        while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t' || source[pos] == '\r' || source[pos] == '\n')) ++pos;
    }
    char take() {
        if (pos >= source.size()) fail("unexpected end of input");
        return source[pos++];
    }
    uint32_t hex4() {
        if (source.size() - pos < 4) fail("incomplete Unicode escape");
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const unsigned char c = static_cast<unsigned char>(source[pos++]);
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            if (digit < 0) fail("invalid hexadecimal digit in Unicode escape");
            value = (value << 4) | static_cast<uint32_t>(digit);
        }
        return value;
    }
    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }
    std::string parseString() {
        if (take() != '"') fail("expected string");
        std::string out;
        while (pos < source.size()) {
            const unsigned char c = static_cast<unsigned char>(take());
            if (c == '"') return out;
            if (c < 0x20) fail("unescaped control character in string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            const char escaped = take();
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (source.size() - pos < 6 || source[pos] != '\\' || source[pos + 1] != 'u')
                            fail("high surrogate must be followed by a low surrogate");
                        pos += 2;
                        const uint32_t low = hex4();
                        if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) fail("unexpected low surrogate");
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("invalid string escape");
            }
            if (out.size() > kMaxInput) fail("decoded string exceeds safety limit");
        }
        fail("unterminated string");
        return {};
    }
    bool consumeLiteral(const char* literal) {
        const size_t length = std::char_traits<char>::length(literal);
        if (source.compare(pos, length, literal) != 0) return false;
        pos += length;
        return true;
    }
    Value parseNumber() {
        const size_t start = pos;
        if (pos < source.size() && source[pos] == '-') ++pos;
        if (pos >= source.size()) fail("incomplete number");
        if (source[pos] == '0') {
            ++pos;
            if (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) fail("leading zero in number");
        } else {
            if (source[pos] < '1' || source[pos] > '9') fail("invalid number");
            while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
        }
        bool floating = false;
        if (pos < source.size() && source[pos] == '.') {
            floating = true; ++pos;
            const size_t digits = pos;
            while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
            if (digits == pos) fail("fraction requires at least one digit");
        }
        if (pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
            floating = true; ++pos;
            if (pos < source.size() && (source[pos] == '+' || source[pos] == '-')) ++pos;
            const size_t digits = pos;
            while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
            if (digits == pos) fail("exponent requires at least one digit");
        }
        const std::string token = source.substr(start, pos - start);
        try {
            if (!floating) {
                try { return Value(static_cast<int64_t>(std::stoll(token))); }
                catch (const std::out_of_range&) { return Value(BigInt::parse(token)); }
            }
            const double number = std::stod(token);
            if (!std::isfinite(number)) fail("number is outside the supported finite range");
            return Value(number);
        } catch (const std::runtime_error&) { throw; }
          catch (const std::exception&) { fail("number is outside the supported range"); }
        return Value{};
    }
    Value parseValue(size_t depth) {
        if (depth > kMaxDepth) fail("maximum nesting depth exceeded");
        whitespace();
        if (pos >= source.size()) fail("expected a value");
        const char c = source[pos];
        if (c == '"') return Value(parseString());
        if (c == '{') {
            ++pos;
            auto out = object();
            whitespace();
            if (pos < source.size() && source[pos] == '}') { ++pos; return Value(out); }
            for (;;) {
                whitespace();
                if (pos >= source.size() || source[pos] != '"') fail("object keys must be strings");
                const std::string key = parseString();
                whitespace();
                if (take() != ':') fail("expected ':' after object key");
                out->fields[key] = parseValue(depth + 1);
                whitespace();
                const char delimiter = take();
                if (delimiter == '}') break;
                if (delimiter != ',') fail("expected ',' or '}' in object");
            }
            return Value(out);
        }
        if (c == '[') {
            ++pos;
            auto out = array();
            whitespace();
            if (pos < source.size() && source[pos] == ']') { ++pos; return Value(out); }
            for (;;) {
                if (out->items.size() >= 1000000) fail("array item limit exceeded");
                out->items.push_back(parseValue(depth + 1));
                whitespace();
                const char delimiter = take();
                if (delimiter == ']') break;
                if (delimiter != ',') fail("expected ',' or ']' in array");
            }
            return Value(out);
        }
        if (consumeLiteral("true")) return Value(true);
        if (consumeLiteral("false")) return Value(false);
        if (consumeLiteral("null")) return Value(Null{});
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail("invalid value");
        return Value{};
    }
public:
    explicit JsonReader(const std::string& input) : source(input) {}
    Value parse() {
        if (source.size() > kMaxInput) fail("input exceeds 64 MiB safety limit");
        Value result = parseValue(0);
        whitespace();
        if (pos != source.size()) fail("unexpected trailing content");
        return result;
    }
};
Value makeFn(const std::string&name){
 Value advanced=createAdvancedBuiltin(name);if(!std::holds_alternative<Undefined>(advanced.data))return advanced;

 if(name=="std:crypto.sha256")return native([](const Args&a){need(a,1,"sha256");return Value(sha256Hex(stringArg(a,0)));});
 if(name=="std:crypto.hmacSha256")return native([](const Args&a){need(a,2,"hmacSha256");return Value(hmacSha256Hex(stringArg(a,0),stringArg(a,1)));});
 if(name=="std:crypto.timingSafeEqual")return native([](const Args&a){need(a,2,"timingSafeEqual");return Value(constantTimeEqual(stringArg(a,0),stringArg(a,1)));});
 if(name=="std:object.keys"||name=="std:object.values"||name=="std:object.entries")return native([name](const Args&a){need(a,1,name);auto p=std::get_if<std::shared_ptr<Object>>(&a[0].data);if(!p)throw std::runtime_error(name+" expects an object");auto out=array();for(const auto&kv:(*p)->fields){if(name=="std:object.keys")out->items.emplace_back(kv.first);else if(name=="std:object.values")out->items.push_back(kv.second);else{auto pair=array();pair->items.emplace_back(kv.first);pair->items.push_back(kv.second);out->items.emplace_back(pair);}}return Value(out);});
 if(name=="std:object.has")return native([](const Args&a){need(a,2,"object.has");auto p=std::get_if<std::shared_ptr<Object>>(&a[0].data);if(!p)throw std::runtime_error("object.has expects an object");return Value((*p)->fields.count(stringArg(a,1))!=0);});
 if(name=="std:object.assign")return native([](const Args&a){need(a,2,"object.assign");auto p=std::get_if<std::shared_ptr<Object>>(&a[0].data);auto q=std::get_if<std::shared_ptr<Object>>(&a[1].data);if(!p||!q)throw std::runtime_error("object.assign expects two objects");for(const auto&kv:(*q)->fields)(*p)->fields[kv.first]=kv.second;return a[0];});
 if(name=="std:object.remove")return native([](const Args&a){need(a,2,"object.remove");auto p=std::get_if<std::shared_ptr<Object>>(&a[0].data);if(!p)throw std::runtime_error("object.remove expects an object");return Value((*p)->fields.erase(stringArg(a,1))!=0);});
 if(name=="std:date.now")return native([](const Args&){auto n=std::chrono::system_clock::now().time_since_epoch();return Value(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(n).count()));});
 if(name=="std:date.toISOString"||name=="std:date.year"||name=="std:date.month"||name=="std:date.day")return native([name](const Args&a){need(a,1,"date operation");std::time_t t=static_cast<std::time_t>(integer(a[0])/1000);std::tm tm{};
#ifdef _WIN32
 gmtime_s(&tm,&t);
#else
 gmtime_r(&t,&tm);
#endif
 if(name=="std:date.year") return Value(tm.tm_year+1900);
 if(name=="std:date.month") return Value(tm.tm_mon+1);
 if(name=="std:date.day") return Value(tm.tm_mday);
 char b[32];
 std::strftime(b,sizeof(b),"%Y-%m-%dT%H:%M:%SZ",&tm);
 return Value(std::string(b));});
 if(name=="std:math.abs")return native([](const Args&a){need(a,1,"abs");double x=std::fabs(number(a[0]));return Value(x);});
 if(name=="std:math.ceil")return native([](const Args&a){need(a,1,"ceil");return Value(std::ceil(number(a[0])));});
 if(name=="std:math.floor")return native([](const Args&a){need(a,1,"floor");return Value(std::floor(number(a[0])));});
 if(name=="std:math.round")return native([](const Args&a){need(a,1,"round");return Value(integer(Value(std::round(number(a[0])))));});
 if(name=="std:math.sqrt")return native([](const Args&a){need(a,1,"sqrt");double x=number(a[0]);if(x<0)throw std::runtime_error("sqrt domain error: value must be >= 0");return Value(std::sqrt(x));});
 if(name=="std:math.pow")return native([](const Args&a){need(a,2,"pow");return Value(std::pow(number(a[0]),number(a[1])));});
 if(name=="std:math.min"||name=="std:math.max")return native([name](const Args&a){need(a,1,name);double x=number(a[0]);for(size_t i=1;i<a.size();++i)x=name=="std:math.min"?std::min(x,number(a[i])):std::max(x,number(a[i]));return Value(x);});
 if(name=="std:math.clamp")return native([](const Args&a){need(a,3,"clamp");double x=number(a[0]),lo=number(a[1]),hi=number(a[2]);if(lo>hi)throw std::runtime_error("clamp minimum cannot exceed maximum");return Value(std::max(lo,std::min(x,hi)));});
 if(name=="std:math.sign")return native([](const Args&a){need(a,1,"sign");double x=number(a[0]);return Value((x>0)-(x<0));});
 if(name=="std:math.random")return native([](const Args&){static thread_local std::mt19937_64 r(std::random_device{}());return Value(std::generate_canonical<double,53>(r));});
 if(name=="std:string.upper"||name=="std:string.lower"||name=="std:string.trim"||name=="std:string.reverse")return native([name](const Args&a){need(a,1,"string operation");std::string s=stringArg(a,0);if(name=="std:string.upper")return Value(upper(s));if(name=="std:string.lower")return Value(lower(s));if(name=="std:string.trim")return Value(trim(s));std::reverse(s.begin(),s.end());return Value(s);});
 if(name=="std:string.contains"||name=="std:string.startsWith"||name=="std:string.endsWith")return native([name](const Args&a){need(a,2,"string predicate");std::string s=stringArg(a,0),q=stringArg(a,1);bool b=name=="std:string.contains"?s.find(q)!=std::string::npos:name=="std:string.startsWith"?s.rfind(q,0)==0:(s.size()>=q.size()&&s.compare(s.size()-q.size(),q.size(),q)==0);return Value(b);});
 if(name=="std:string.replace")return native([](const Args&a){need(a,3,"replace");std::string s=stringArg(a,0),from=stringArg(a,1),to=stringArg(a,2);if(from.empty())return Value(s);size_t p=0;while((p=s.find(from,p))!=std::string::npos){s.replace(p,from.size(),to);p+=to.size();}return Value(s);});
 if(name=="std:string.split")return native([](const Args&a){need(a,2,"split");std::string s=stringArg(a,0),sep=stringArg(a,1);auto r=array();if(sep.empty()){for(char c:s)r->items.emplace_back(std::string(1,c));}else{size_t p=0,q;while((q=s.find(sep,p))!=std::string::npos){r->items.emplace_back(s.substr(p,q-p));p=q+sep.size();}r->items.emplace_back(s.substr(p));}return Value(r);});
 if(name=="std:string.repeat")return native([](const Args&a){need(a,2,"repeat");auto s=stringArg(a,0);int64_t n=integer(a[1]);if(n<0||n>1000000)throw std::runtime_error("repeat count is out of range");std::string o;for(int64_t i=0;i<n;++i)o+=s;return Value(o);});
 if(name=="std:array.length")return native([](const Args&a){need(a,1,"length");return Value(static_cast<int64_t>(asArray(a[0])->items.size()));});
 if(name=="std:array.push")return native([](const Args&a){need(a,2,"push");auto x=asArray(a[0]);for(size_t i=1;i<a.size();++i)x->items.push_back(a[i]);return Value(static_cast<int64_t>(x->items.size()));});
 if(name=="std:array.pop")return native([](const Args&a){need(a,1,"pop");auto x=asArray(a[0]);if(x->items.empty())return undef();Value v=x->items.back();x->items.pop_back();return v;});
 if(name=="std:array.contains")return native([](const Args&a){need(a,2,"contains");for(const auto&v:asArray(a[0])->items)if(valueToString(v)==valueToString(a[1])&&valueType(v)==valueType(a[1]))return Value(true);return Value(false);});
 if(name=="std:array.join")return native([](const Args&a){need(a,1,"join");std::string sep=a.size()>1?stringArg(a,1):",";auto x=asArray(a[0]);std::string out;for(size_t i=0;i<x->items.size();++i){if(i)out+=sep;out+=valueToString(x->items[i]);}return Value(out);});
 if(name=="std:array.reverse")return native([](const Args&a){need(a,1,"reverse");auto x=asArray(a[0]);std::reverse(x->items.begin(),x->items.end());return a[0];});
 if(name=="std:array.sort")return native([](const Args&a){need(a,1,"sort");auto x=asArray(a[0]);std::stable_sort(x->items.begin(),x->items.end(),[](const Value&l,const Value&r){if((valueType(l)=="int"||valueType(l)=="float")&&(valueType(r)=="int"||valueType(r)=="float"))return std::stod(valueToString(l))<std::stod(valueToString(r));return valueToString(l)<valueToString(r);});return a[0];});
 if(name=="std:json.stringify")return native([](const Args&a){need(a,1,"stringify");return Value(valueToJson(a[0],a.size()>1&&isTruthy(a[1])));});
 if(name=="std:json.parse")return native([](const Args&a){need(a,1,"parse");return JsonReader(stringArg(a,0)).parse();});
 if(name=="std:time.now")return native([](const Args&){auto n=std::chrono::system_clock::now().time_since_epoch();return Value(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(n).count()));});
 if(name=="std:time.sleep")return native([](const Args&a){need(a,1,"sleep");auto n=integer(a[0]);if(n<0||n>86400000)throw std::runtime_error("sleep duration must be between 0 and 86400000 ms");std::this_thread::sleep_for(std::chrono::milliseconds(n));return Value(Null{});});
 if(name=="std:time.measure")return native([](const Args&){static auto start=std::chrono::steady_clock::now();auto n=std::chrono::steady_clock::now();return Value(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(n-start).count()));});
 if(name=="std:encoding.base64Encode")return native([](const Args&a){need(a,1,"base64Encode");return Value(base64Encode(stringArg(a,0)));});
 if(name=="std:encoding.base64Decode")return native([](const Args&a){need(a,1,"base64Decode");return Value(base64Decode(stringArg(a,0)));});
 if(name=="std:encoding.hexEncode")return native([](const Args&a){need(a,1,"hexEncode");return Value(hexEncode(stringArg(a,0)));});
 if(name=="std:encoding.hexDecode")return native([](const Args&a){need(a,1,"hexDecode");return Value(hexDecode(stringArg(a,0)));});
 if(name=="std:encoding.urlEncode"||name=="std:url.encode")return native([](const Args&a){need(a,1,"urlEncode");return Value(urlEncode(stringArg(a,0)));});
 if(name=="std:encoding.urlDecode"||name=="std:url.decode")return native([](const Args&a){need(a,1,"urlDecode");return Value(urlDecode(stringArg(a,0)));});
 if(name=="std:fs.readText")return native([](const Args&a){need(a,1,"readText");const std::string path=stringArg(a,0);std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read file: "+path);f.seekg(0,std::ios::end);const std::streamoff size=f.tellg();if(size<0)throw std::runtime_error("Cannot determine file size: "+path);if(size>64LL*1024LL*1024LL)throw std::runtime_error("File exceeds 64 MiB readText safety limit: "+path);f.seekg(0,std::ios::beg);std::string out(static_cast<size_t>(size),'\0');if(size>0&&!f.read(&out[0],size))throw std::runtime_error("Failed while reading file: "+path);return Value(out);});
 if(name=="std:fs.writeText"||name=="std:fs.appendText")return native([name](const Args&a){need(a,2,name);const std::string path=stringArg(a,0);const std::string content=valueToString(a[1]);if(content.size()>64u*1024u*1024u)throw std::runtime_error(name+" input exceeds 64 MiB safety limit");std::ofstream f(path,std::ios::binary|(name=="std:fs.appendText"?std::ios::app:std::ios::trunc));if(!f)throw std::runtime_error("Cannot write file: "+path);f.write(content.data(),static_cast<std::streamsize>(content.size()));if(!f)throw std::runtime_error("Failed while writing file: "+path);return Value(true);});
 if(name=="std:fs.exists")return native([](const Args&a){need(a,1,"exists");std::error_code ec;const bool result=std::filesystem::exists(stringArg(a,0),ec);if(ec)throw std::runtime_error("fs.exists failed: "+ec.message());return Value(result);});
 if(name=="std:fs.remove")return native([](const Args&a){need(a,1,"remove");std::error_code ec;bool r=std::filesystem::remove(stringArg(a,0),ec);if(ec)throw std::runtime_error(ec.message());return Value(r);});
 if(name=="std:fs.mkdir")return native([](const Args&a){need(a,1,"mkdir");std::error_code ec;bool r=std::filesystem::create_directories(stringArg(a,0),ec);if(ec)throw std::runtime_error(ec.message());return Value(r);});
 if(name=="std:fs.list")return native([](const Args&a){need(a,1,"list");const std::string path=stringArg(a,0);auto r=array();std::error_code ec;std::filesystem::directory_iterator it(path,ec),end;if(ec)throw std::runtime_error("Cannot list directory `"+path+"`: "+ec.message());for(;it!=end;it.increment(ec)){if(ec)throw std::runtime_error("Directory iteration failed: "+ec.message());r->items.emplace_back(it->path().filename().string());if(r->items.size()>=1000000)throw std::runtime_error("fs.list exceeds 1000000 entry safety limit");}std::sort(r->items.begin(),r->items.end(),[](const Value&x,const Value&y){return std::get<std::string>(x.data)<std::get<std::string>(y.data);});return Value(r);});
 if(name=="std:path.join")return native([](const Args&a){need(a,1,"join");std::filesystem::path p;for(auto&v:a)p/=valueToString(v);return Value(p.lexically_normal().string());});
 if(name=="std:path.basename")return native([](const Args&a){need(a,1,"basename");return Value(std::filesystem::path(stringArg(a,0)).filename().string());});
 if(name=="std:path.dirname")return native([](const Args&a){need(a,1,"dirname");return Value(std::filesystem::path(stringArg(a,0)).parent_path().string());});
 if(name=="std:path.extension")return native([](const Args&a){need(a,1,"extension");return Value(std::filesystem::path(stringArg(a,0)).extension().string());});
 if(name=="std:path.normalize")return native([](const Args&a){need(a,1,"normalize");return Value(std::filesystem::path(stringArg(a,0)).lexically_normal().string());});
 if(name=="std:path.isAbsolute")return native([](const Args&a){need(a,1,"isAbsolute");return Value(std::filesystem::path(stringArg(a,0)).is_absolute());});
 if(name=="std:env.get")return native([](const Args&a){need(a,1,"get");const char*v=std::getenv(stringArg(a,0).c_str());return v?Value(std::string(v)):undef();});
 if(name=="std:env.has")return native([](const Args&a){need(a,1,"has");return Value(std::getenv(stringArg(a,0).c_str())!=nullptr);});
 if(name=="std:env.set")return native([](const Args&a){need(a,2,"set");
#ifdef _WIN32
 int r=_putenv_s(stringArg(a,0).c_str(),stringArg(a,1).c_str());
#else
 int r=setenv(stringArg(a,0).c_str(),stringArg(a,1).c_str(),1);
#endif
 if(r!=0) throw std::runtime_error("Failed to set environment variable");
 return Value(true);});
 if(name=="std:process.cwd"||name=="std:system.cwd")return native([](const Args&){return Value(std::filesystem::current_path().string());});
 if(name=="std:process.platform"||name=="std:system.platform")return native([](const Args&){
#ifdef _WIN32
 return Value("windows");
#elif __APPLE__
 return Value("macos");
#elif __linux__
 return Value("linux");
#else
 return Value("unknown");
#endif
 });
 if(name=="std:system.arch")return native([](const Args&){
#if defined(__x86_64__) || defined(_M_X64)
 return Value("x64");
#elif defined(__aarch64__) || defined(_M_ARM64)
 return Value("arm64");
#else
 return Value("unknown");
#endif
 });
 if(name=="std:system.eol")return native([](const Args&){
#ifdef _WIN32
 return Value("\r\n");
#else
 return Value("\n");
#endif
 });
 if(name=="std:random.float")return native([](const Args&){static thread_local std::mt19937_64 r(std::random_device{}());return Value(std::generate_canonical<double,53>(r));});
 if(name=="std:random.int")return native([](const Args&a){need(a,2,"random.int");int64_t lo=integer(a[0]),hi=integer(a[1]);if(lo>hi)throw std::runtime_error("random.int minimum exceeds maximum");static thread_local std::mt19937_64 r(std::random_device{}());return Value(std::uniform_int_distribution<int64_t>(lo,hi)(r));});
 if(name=="std:random.bool")return native([](const Args&){static thread_local std::mt19937_64 r(std::random_device{}());return Value((r()&1)!=0);});
 if(name=="std:random.choice")return native([](const Args&a){need(a,1,"random.choice");auto x=asArray(a[0]);if(x->items.empty())return undef();static thread_local std::mt19937_64 r(std::random_device{}());return x->items[std::uniform_int_distribution<size_t>(0,x->items.size()-1)(r)];});
 if(name=="std:console.log"||name=="std:console.info"||name=="std:console.warn"||name=="std:console.error"||name=="std:console.debug"||name=="std:console.table"||name=="std:console.clear"){auto c=createGlobal("console",{});auto o=std::get<std::shared_ptr<Object>>(c.data);return o->fields[name.substr(std::string("std:console.").size())];}
 if(name=="std:uuid.v4")return native([](const Args&){std::random_device source;std::uniform_int_distribution<int> distribution(0,255);unsigned char bytes[16];for(auto& byte:bytes)byte=static_cast<unsigned char>(distribution(source));bytes[6]=static_cast<unsigned char>((bytes[6]&0x0f)|0x40);bytes[8]=static_cast<unsigned char>((bytes[8]&0x3f)|0x80);std::ostringstream out;out<<std::hex<<std::setfill('0');for(int i=0;i<16;i++){out<<std::setw(2)<<static_cast<int>(bytes[i]);if(i==3||i==5||i==7||i==9)out<<'-';}return Value(out.str());});
 if(name=="std:regex.test"||name=="std:regex.match"||name=="std:regex.replace")return native([name](const Args&a){need(a,name=="std:regex.replace"?3:2,"regex operation");std::regex re(stringArg(a,1));std::string s=stringArg(a,0);if(name=="std:regex.test")return Value(std::regex_search(s,re));if(name=="std:regex.replace")return Value(std::regex_replace(s,re,stringArg(a,2)));std::smatch m;if(!std::regex_search(s,m,re))return undef();auto ar=array();for(auto&x:m)ar->items.emplace_back(x.str());return Value(ar);});
 if(name=="std:testing.assert"||name=="std:testing.equal"||name=="std:testing.notEqual")return native([name](const Args&a){need(a,name=="std:testing.assert"?1:2,name);bool ok=false;if(name=="std:testing.assert")ok=isTruthy(a[0]);else ok=(valueType(a[0])==valueType(a[1])&&valueToString(a[0])==valueToString(a[1]));if(name=="std:testing.notEqual")ok=!ok;if(!ok)throw std::runtime_error(a.size()>2?stringArg(a,2):"Assertion failed: "+name);return Value(true);});
 if(name=="std:url.parse")return native([](const Args&a){need(a,1,"url.parse");std::string s=stringArg(a,0);auto o=object();size_t p=s.find("://");if(p==std::string::npos){o->fields["scheme"]=Value("");o->fields["host"]=Value("");o->fields["path"]=Value(s);}else{ o->fields["scheme"]=Value(s.substr(0,p));size_t h=p+3,e=s.find_first_of("/?#",h);if(e==std::string::npos)e=s.size();o->fields["host"]=Value(s.substr(h,e-h));size_t q=s.find('?',e),hash=s.find('#',e),end=std::min(q==std::string::npos?s.size():q,hash==std::string::npos?s.size():hash);o->fields["path"]=Value(s.substr(e,end-e));o->fields["query"]=Value(q==std::string::npos?"":s.substr(q+1,(hash==std::string::npos?s.size():hash)-q-1));o->fields["fragment"]=Value(hash==std::string::npos?"":s.substr(hash+1));}return Value(o);});
 if(name=="std:csv.parse")return native([](const Args&a){
    need(a,1,"csv.parse");const std::string input=stringArg(a,0);if(input.size()>64u*1024u*1024u)throw std::runtime_error("CSV input exceeds 64 MiB safety limit");
    auto rows=array();auto row=array();std::string cell;bool quoted=false,afterQuote=false,fieldStarted=false;
    auto finishField=[&](){row->items.emplace_back(cell);cell.clear();fieldStarted=false;afterQuote=false;};
    auto finishRow=[&](){finishField();rows->items.emplace_back(row);row=array();};
    for(size_t i=0;i<input.size();++i){const char c=input[i];
        if(quoted){if(c=='"'){if(i+1<input.size()&&input[i+1]=='"'){cell.push_back('"');++i;}else{quoted=false;afterQuote=true;}}else cell.push_back(c);continue;}
        if(afterQuote){if(c==','){finishField();continue;}if(c=='\n'){finishRow();continue;}if(c=='\r'){if(i+1<input.size()&&input[i+1]=='\n')++i;finishRow();continue;}throw std::runtime_error("CSV parse error: unexpected character after closing quote at byte "+std::to_string(i));}
        if(c=='"'){if(fieldStarted||!cell.empty())throw std::runtime_error("CSV parse error: quote inside unquoted field at byte "+std::to_string(i));quoted=true;fieldStarted=true;continue;}
        if(c==','){finishField();continue;}
        if(c=='\n'){finishRow();continue;}
        if(c=='\r'){if(i+1<input.size()&&input[i+1]=='\n')++i;finishRow();continue;}
        cell.push_back(c);fieldStarted=true;
    }
    if(quoted)throw std::runtime_error("CSV parse error: unterminated quoted field");
    if(fieldStarted||!cell.empty()||!row->items.empty())finishRow();
    return Value(rows);
 });
 if(name=="std:csv.stringify")return native([](const Args&a){need(a,1,"csv.stringify");auto rows=asArray(a[0]);std::string out;for(size_t r=0;r<rows->items.size();++r){auto row=asArray(rows->items[r]);for(size_t c=0;c<row->items.size();++c){if(c)out+=',';std::string s=valueToString(row->items[c]);if(s.find_first_of(",\"\r\n")!=std::string::npos){std::string t="\"";for(char x:s){if(x=='"')t+='"';t+=x;}s=t+'"';}out+=s;}if(r+1<rows->items.size())out+='\n';}return Value(out);});
 if(name=="std:format.padStart"||name=="std:format.padEnd")return native([name](const Args&a){need(a,2,name);std::string value=stringArg(a,0);int64_t width=integer(a[1]);if(width<0||width>1000000)throw std::runtime_error("format width must be between 0 and 1000000");std::string fill=a.size()>2?stringArg(a,2):" ";if(static_cast<size_t>(width)<=value.size()||fill.empty())return Value(value);std::string padding;while(padding.size()<static_cast<size_t>(width)-value.size())padding+=fill;padding.resize(static_cast<size_t>(width)-value.size());return Value(name=="std:format.padStart"?padding+value:value+padding);});
 if(name=="std:format.truncate")return native([](const Args&a){need(a,2,"truncate");std::string value=stringArg(a,0);int64_t width=integer(a[1]);if(width<0||width>1000000)throw std::runtime_error("truncate length must be between 0 and 1000000");if(value.size()<=static_cast<size_t>(width))return Value(value);std::string suffix=a.size()>2?stringArg(a,2):"...";return Value(value.substr(0,static_cast<size_t>(width))+suffix);});
 if(name=="std:format.capitalize"||name=="std:format.camelCase"||name=="std:format.snakeCase"||name=="std:format.kebabCase")return native([name](const Args&a){need(a,1,name);std::string input=stringArg(a,0);std::vector<std::string> words;std::string word;for(size_t i=0;i<input.size();++i){unsigned char c=static_cast<unsigned char>(input[i]);if(std::isalnum(c)){if(std::isupper(c)&&!word.empty()&&i>0&&std::islower(static_cast<unsigned char>(input[i-1]))){words.push_back(lower(word));word.clear();}word+=static_cast<char>(c);}else if(!word.empty()){words.push_back(lower(word));word.clear();}}if(!word.empty())words.push_back(lower(word));if(name=="std:format.capitalize"){std::string out=lower(input);if(!out.empty())out[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));return Value(out);}std::string out;for(size_t i=0;i<words.size();++i){if(name=="std:format.camelCase"){if(i==0)out+=words[i];else{std::string w=words[i];if(!w.empty())w[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));out+=w;}}else{if(i)out+=(name=="std:format.snakeCase"?"_":"-");out+=words[i];}}return Value(out);});
 if(name=="std:statistics.sum"||name=="std:statistics.mean"||name=="std:statistics.median"||name=="std:statistics.min"||name=="std:statistics.max"||name=="std:statistics.variance"||name=="std:statistics.standardDeviation")return native([name](const Args&a){need(a,1,name);auto input=asArray(a[0]);if(input->items.empty()){if(name=="std:statistics.sum")return Value(0.0);throw std::runtime_error(name+" requires a non-empty array");}std::vector<double> values;values.reserve(input->items.size());double total=0.0;for(const auto&item:input->items){double v=number(item);values.push_back(v);total+=v;}if(name=="std:statistics.sum")return Value(total);if(name=="std:statistics.mean")return Value(total/static_cast<double>(values.size()));if(name=="std:statistics.min")return Value(*std::min_element(values.begin(),values.end()));if(name=="std:statistics.max")return Value(*std::max_element(values.begin(),values.end()));if(name=="std:statistics.median"){std::sort(values.begin(),values.end());size_t mid=values.size()/2;return Value(values.size()%2?values[mid]:(values[mid-1]+values[mid])/2.0);}double mean=total/static_cast<double>(values.size()),variance=0.0;for(double v:values){double delta=v-mean;variance+=delta*delta;}variance/=static_cast<double>(values.size());return Value(name=="std:statistics.variance"?variance:std::sqrt(variance));});

 return undef();
}
}
Value createBuiltinModule(const std::string& name,const std::vector<std::string>&){if(!hasModule(name))throw std::runtime_error("Unknown standard module `"+name+"`");Value advancedModule=createAdvancedModule(name);if(!std::holds_alternative<Undefined>(advancedModule.data))return advancedModule;auto o=object();o->moduleName=name;for(const auto&m:builtinModules())if(m.name==name){for(const auto&e:m.exports){Value f=makeFn(name+"."+e);if(std::holds_alternative<Undefined>(f.data))throw std::runtime_error("Standard member declared but not implemented: "+name+"."+e);o->fields[e]=f;}break;}if(name=="std:launch"){Value request=makeFn("std:launch.request");auto callable=std::get<NativeFunction>(request.data);o->fields["__call"]=Value(NativeFunction([callable](const std::vector<Value>&args){return callable(args);}));}return Value(o);}
Value createGlobal(const std::string& name,const std::vector<std::string>&){
 if(name=="console"||name=="std:console"){auto o=object();auto output=[](const std::string&prefix,bool err,const Args&a){std::ostream&out=err?std::cerr:std::cout;if(!prefix.empty())out<<prefix;for(size_t i=0;i<a.size();++i){if(i)out<<' ';out<<valueToString(a[i]);}out<<'\n';return Value(Null{});};o->fields["log"]=native([output](const Args&a){return output("",false,a);});o->fields["info"]=native([output](const Args&a){return output("",false,a);});o->fields["warn"]=native([output](const Args&a){return output("Warning: ",true,a);});o->fields["error"]=native([output](const Args&a){return output("Error: ",true,a);});o->fields["debug"]=native([output](const Args&a){return output("",false,a);});o->fields["table"]=native([output](const Args&a){if(a.empty())return Value(Null{});return output("",false,{Value(valueToJson(a[0],true))});});o->fields["clear"] =native([](const Args&){std::cout<<"\033[2J\033[H";return Value(Null{});});return Value(o);}
 if(name=="Number") {auto o=object();o->fields["MAX_SAFE_INTEGER"]=Value(int64_t(9007199254740991LL));o->fields["MIN_SAFE_INTEGER"]=Value(int64_t(-9007199254740991LL));o->fields["MAX_VALUE"]=Value(std::numeric_limits<double>::max());o->fields["MIN_VALUE"]=Value(std::numeric_limits<double>::denorm_min());o->fields["NaN"]=Value(std::numeric_limits<double>::quiet_NaN());o->fields["EPSILON"]=Value(std::numeric_limits<double>::epsilon());o->fields["isNaN"]=native([](const Args&a){need(a,1,"Number.isNaN");if(auto p=std::get_if<double>(&a[0].data))return Value(std::isnan(*p));return Value(false);});o->fields["isFinite"]=native([](const Args&a){need(a,1,"Number.isFinite");if(auto p=std::get_if<double>(&a[0].data))return Value(std::isfinite(*p));if(std::holds_alternative<int64_t>(a[0].data))return Value(true);return Value(false);});o->fields["isInteger"]=native([](const Args&a){need(a,1,"Number.isInteger");if(std::holds_alternative<int64_t>(a[0].data))return Value(true);if(auto p=std::get_if<double>(&a[0].data))return Value(std::isfinite(*p)&&std::floor(*p)==*p);return Value(false);});o->fields["isSafeInteger"]=native([](const Args&a){need(a,1,"Number.isSafeInteger");if(auto p=std::get_if<int64_t>(&a[0].data))return Value(*p>=-9007199254740991LL&&*p<=9007199254740991LL);if(auto p=std::get_if<double>(&a[0].data))return Value(std::isfinite(*p)&&std::floor(*p)==*p&&std::fabs(*p)<=9007199254740991.0);return Value(false);});o->fields["parseInt"]=native([](const Args&a){need(a,1,"Number.parseInt");std::string s=trim(stringArg(a,0));size_t n=0;long long v=std::stoll(s,&n,a.size()>1?static_cast<int>(integer(a[1])):10);if(n==0)throw std::runtime_error("Invalid integer input");return Value(static_cast<int64_t>(v));});o->fields["parseFloat"]=native([](const Args&a){need(a,1,"Number.parseFloat");std::string s=trim(stringArg(a,0));size_t n=0;double v=std::stod(s,&n);if(n==0)throw std::runtime_error("Invalid float input");return Value(v);});o->fields["POSITIVE_INFINITY"]=Value(std::numeric_limits<double>::infinity());o->fields["NEGATIVE_INFINITY"]=Value(-std::numeric_limits<double>::infinity());return Value(o);}
 if(name=="print")return native([](const Args&a){for(size_t i=0;i<a.size();++i){if(i)std::cout<<' ';std::cout<<valueToString(a[i]);}std::cout<<'\n';return Value(Null{});});
 if(name=="type")return native([](const Args&a){need(a,1,"type");return Value(valueType(a[0]));});
 if(name=="str"||name=="String")return native([](const Args&a){need(a,1,"String");return Value(valueToString(a[0]));});
 if(name=="int")return native([](const Args&a){need(a,1,"int");if(auto b=std::get_if<BigInt>(&a[0].data)){try{return Value(static_cast<int64_t>(std::stoll(b->toString())));}catch(...){throw std::runtime_error("BigInt is outside int64 range");}}double d=number(a[0]);if(std::floor(d)!=d)throw std::runtime_error("int() does not truncate fractional values; use floor() or round()");return Value(integer(a[0]));});
 if(name=="bigint"||name=="BigInt")return native([](const Args&a){need(a,1,"bigint");if(auto b=std::get_if<BigInt>(&a[0].data))return Value(*b);return Value(BigInt::parse(valueToString(a[0])));});
 if(name=="float"||name=="Number")return native([](const Args&a){need(a,1,"Number");return Value(number(a[0]));});
 if(name=="bool")return native([](const Args&a){need(a,1,"bool");return Value(isTruthy(a[0]));});
 if(name=="len")return native([](const Args&a){need(a,1,"len");if(auto p=std::get_if<std::string>(&a[0].data))return Value(static_cast<int64_t>(p->size()));if(auto p=std::get_if<std::shared_ptr<Array>>(&a[0].data))return Value(static_cast<int64_t>((*p)->items.size()));throw std::runtime_error("len() expects string or array");});
 if(name=="isUndefined")return native([](const Args&a){need(a,1,"isUndefined");return Value(std::holds_alternative<Undefined>(a[0].data));});
 if(name=="assert")return native([](const Args&a){need(a,1,"assert");if(!isTruthy(a[0]))throw std::runtime_error(a.size()>1?stringArg(a,1):"Assertion failed");return Value(true);});
 if(name=="Error")return native([](const Args&a){return Value(a.empty()?"Error":stringArg(a,0));});
 if(name=="input")return native([](const Args&a){if(!a.empty()){std::cout<<valueToString(a[0]);std::cout.flush();}std::string line;if(!std::getline(std::cin,line))return Value(Null{});return Value(line);});
 if(name=="range")return native([](const Args&a){need(a,1,"range");int64_t start=0,end=integer(a[0]),step=1;if(a.size()>1){start=integer(a[0]);end=integer(a[1]);}if(a.size()>2)step=integer(a[2]);if(step==0)throw std::runtime_error("range step cannot be zero");auto out=array();for(int64_t x=start;(step>0?x<end:x>end);){if(out->items.size()>=1000000)throw std::runtime_error("range exceeds 1000000 elements");out->items.emplace_back(x);if((step>0&&x>INT64_MAX-step)||(step<0&&x<INT64_MIN-step))break;x+=step;}return Value(out);});
 if(name=="Array") {auto o=object();o->fields["__call"]=native([](const Args&a){auto x=array();x->items=a;return Value(x);});o->fields["isArray"]=native([](const Args&a){need(a,1,"Array.isArray");return Value(std::holds_alternative<std::shared_ptr<Array>>(a[0].data));});o->fields["from"]=native([](const Args&a){need(a,1,"Array.from");auto x=array();if(auto p=std::get_if<std::shared_ptr<Array>>(&a[0].data))x->items=(*p)->items;else if(auto p=std::get_if<std::string>(&a[0].data)){for(char c:*p)x->items.emplace_back(std::string(1,c));}else throw std::runtime_error("Array.from expects an array or string");return Value(x);});return Value(o);}
 if(name=="Object") {auto o=object();o->fields["__call"]=native([](const Args&a){if(a.empty())return Value(object());if(auto p=std::get_if<std::shared_ptr<Object>>(&a[0].data)){auto copy=object();copy->fields=(*p)->fields;return Value(copy);}throw std::runtime_error("Object() accepts an object or no arguments");});o->fields["keys"]=makeFn("std:object.keys");o->fields["values"]=makeFn("std:object.values");o->fields["entries"]=makeFn("std:object.entries");o->fields["has"]=makeFn("std:object.has");o->fields["assign"]=makeFn("std:object.assign");o->fields["remove"]=makeFn("std:object.remove");return Value(o);}
 return undef();
}
}
