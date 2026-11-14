#include "astra/advanced_stdlib.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra { namespace {
using Args = std::vector<Value>;
Value undefinedValue() { return Value{}; }
Value native(NativeFunction f) { return Value(std::move(f)); }
void need(const Args& args, size_t count, const std::string& name) {
    if (args.size() < count) throw std::runtime_error(name + " expects at least " + std::to_string(count) + " argument(s)");
}
std::string stringArg(const Args& args, size_t index, const std::string& name) {
    if (index >= args.size() || !std::holds_alternative<std::string>(args[index].data))
        throw std::runtime_error(name + " expects string arguments");
    return std::get<std::string>(args[index].data);
}
std::string lowerAscii(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
void appendUtf8(std::string& out, uint32_t cp) {
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
uint32_t parseEntityCodepoint(const std::string& entity) {
    if (entity.size() < 2 || entity[0] != '#') throw std::runtime_error("Invalid HTML numeric entity");
    size_t pos = 1;
    int base = 10;
    if (pos < entity.size() && (entity[pos] == 'x' || entity[pos] == 'X')) { base = 16; ++pos; }
    if (pos == entity.size()) throw std::runtime_error("Invalid HTML numeric entity");
    uint32_t value = 0;
    for (; pos < entity.size(); ++pos) {
        int digit = base == 16 ? hexDigit(entity[pos]) : (entity[pos] >= '0' && entity[pos] <= '9' ? entity[pos] - '0' : -1);
        if (digit < 0 || digit >= base || value > (0x10ffffu - static_cast<uint32_t>(digit)) / static_cast<uint32_t>(base))
            throw std::runtime_error("HTML numeric entity is outside the Unicode range");
        value = value * static_cast<uint32_t>(base) + static_cast<uint32_t>(digit);
    }
    if (value == 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        throw std::runtime_error("HTML numeric entity is not a valid Unicode scalar value");
    return value;
}
std::string htmlUnescape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size();) {
        if (input[i] != '&') { out.push_back(input[i++]); continue; }
        const size_t semi = input.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 16) { out.push_back(input[i++]); continue; }
        const std::string entity = input.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos" || entity == "#39") out.push_back('\'');
        else if (!entity.empty() && entity[0] == '#') appendUtf8(out, parseEntityCodepoint(entity));
        else { out.append(input, i, semi - i + 1); i = semi + 1; continue; }
        i = semi + 1;
        if (out.size() > 64u * 1024u * 1024u) throw std::runtime_error("HTML output exceeds 64 MiB safety limit");
    }
    return out;
}
std::string htmlEscape(const std::string& input) {
    if (input.size() > 64u * 1024u * 1024u) throw std::runtime_error("HTML input exceeds 64 MiB safety limit");
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
        if (out.size() > 64u * 1024u * 1024u) throw std::runtime_error("Escaped HTML output exceeds 64 MiB safety limit");
    }
    return out;
}
struct MimeEntry { const char* extension; const char* type; };
const std::vector<MimeEntry>& mimeEntries() {
    static const std::vector<MimeEntry> entries = {
        // Web documents and source formats
        {".html","text/html"},{".htm","text/html"},{".shtml","text/html"},{".xhtml","application/xhtml+xml"},
        {".css","text/css"},{".js","text/javascript"},{".mjs","text/javascript"},{".cjs","text/javascript"},
        {".jsx","text/jsx"},{".ts","text/typescript"},{".tsx","text/tsx"},{".json","application/json"},{".map","application/json"},{".jsonld","application/ld+json"},{".webmanifest","application/manifest+json"},
        {".txt","text/plain"},{".text","text/plain"},{".log","text/plain"},{".md","text/markdown"},
        {".markdown","text/markdown"},{".csv","text/csv"},{".tsv","text/tab-separated-values"},
        {".xml","application/xml"},{".xsl","application/xslt+xml"},{".xslt","application/xslt+xml"},
        {".yaml","application/yaml"},{".yml","application/yaml"},{".toml","application/toml"},
        {".rtf","application/rtf"},{".ics","text/calendar"},{".vcf","text/vcard"},{".sql","application/sql"},
        {".graphql","application/graphql"},{".gql","application/graphql"},{".sh","application/x-sh"},
        {".php","application/x-httpd-php"},{".wasm","application/wasm"},
        // Images
        {".apng","image/apng"},{".avif","image/avif"},{".bmp","image/bmp"},{".gif","image/gif"},
        {".heic","image/heic"},{".heif","image/heif"},{".jxl","image/jxl"},{".jp2","image/jp2"},
        {".jpg","image/jpeg"},{".jpeg","image/jpeg"},{".jpe","image/jpeg"},{".png","image/png"},
        {".svg","image/svg+xml"},{".svgz","image/svg+xml"},{".tif","image/tiff"},{".tiff","image/tiff"},
        {".webp","image/webp"},{".ico","image/vnd.microsoft.icon"},{".cur","image/vnd.microsoft.icon"},
        // Audio and video
        {".aac","audio/aac"},{".aif","audio/aiff"},{".aiff","audio/aiff"},{".mid","audio/midi"},
        {".midi","audio/midi"},{".m4a","audio/mp4"},{".mp3","audio/mpeg"},{".oga","audio/ogg"},
        {".ogg","audio/ogg"},{".opus","audio/opus"},{".wav","audio/wav"},{".weba","audio/webm"},
        {".flac","audio/flac"},{".amr","audio/amr"},{".mp4","video/mp4"},{".m4v","video/mp4"},
        {".mpeg","video/mpeg"},{".mpg","video/mpeg"},{".ogv","video/ogg"},{".mov","video/quicktime"},
        {".webm","video/webm"},{".avi","video/x-msvideo"},{".mkv","video/x-matroska"},
        {".3gp","video/3gpp"},{".3g2","video/3gpp2"},{".tsvideo","video/mp2t"},{".m2ts","video/mp2t"},
        // Fonts
        {".otf","font/otf"},{".ttf","font/ttf"},{".ttc","font/collection"},{".woff","font/woff"},{".woff2","font/woff2"},
        // Archives and compressed data
        {".tar.gz","application/gzip"},{".tgz","application/gzip"},{".br","application/brotli"},
        {".gz","application/gzip"},{".bz","application/x-bzip"},{".bz2","application/x-bzip2"},
        {".xz","application/x-xz"},{".zst","application/zstd"},{".7z","application/x-7z-compressed"},
        {".rar","application/vnd.rar"},{".zip","application/zip"},{".tar","application/x-tar"},
        {".jar","application/java-archive"},{".war","application/java-archive"},{".epub","application/epub+zip"},
        {".dmg","application/x-apple-diskimage"},{".iso","application/x-iso9660-image"},{".bin","application/octet-stream"},
        // Documents and Office formats
        {".pdf","application/pdf"},{".doc","application/msword"},
        {".docx","application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls","application/vnd.ms-excel"},
        {".xlsx","application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt","application/vnd.ms-powerpoint"},
        {".pptx","application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {".odt","application/vnd.oasis.opendocument.text"},{".ods","application/vnd.oasis.opendocument.spreadsheet"},
        {".odp","application/vnd.oasis.opendocument.presentation"},{".pages","application/x-iwork-pages-sffpages"},
        {".numbers","application/x-iwork-numbers-sffnumbers"},{".key","application/x-iwork-keynote-sffkey"},
        // Common application payloads and binaries
        {".bin","application/octet-stream"},{".exe","application/vnd.microsoft.portable-executable"},
        {".dll","application/vnd.microsoft.portable-executable"},{".so","application/octet-stream"},
        {".dylib","application/octet-stream"},{".apk","application/vnd.android.package-archive"},
        {".aab","application/x-authorware-bin"},{".wasm","application/wasm"},
        {".form","application/x-www-form-urlencoded"},{".pem","application/x-pem-file"},
        {".crt","application/x-x509-ca-cert"},{".cer","application/pkix-cert"},{".der","application/pkix-cert"},
        {".p12","application/x-pkcs12"},{".pfx","application/x-pkcs12"},{".7z.001","application/x-7z-compressed"}
    };
    return entries;
}
std::string cookieEncode(const std::string& input) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : input) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            std::string("!#$&+-.^_`|~").find(static_cast<char>(c)) != std::string::npos;
        if (safe) out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}
std::string cookieDecode(const std::string& input) {
    std::string out;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '%') { out.push_back(input[i]); continue; }
        if (i + 2 >= input.size()) throw std::runtime_error("Invalid percent escape in cookie value");
        int hi = hexDigit(input[i + 1]), lo = hexDigit(input[i + 2]);
        if (hi < 0 || lo < 0) throw std::runtime_error("Invalid percent escape in cookie value");
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return out;
}
bool cookieTokenChar(unsigned char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return true;
    return std::string("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string::npos;
}
void validateCookieAttribute(const std::string& value, const std::string& label) {
    if (std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f || c == ';'; }))
        throw std::runtime_error("cookie.serialize option `" + label + "` contains an invalid character");
}
} // namespace

Value createWebBuiltin(const std::string& name) {
    if (name == "std:html.escape") return native([](const Args& args) { need(args, 1, "html.escape"); return Value(htmlEscape(stringArg(args, 0, "html.escape"))); });
    if (name == "std:html.unescape") return native([](const Args& args) { need(args, 1, "html.unescape"); return Value(htmlUnescape(stringArg(args, 0, "html.unescape"))); });

    if (name == "std:mime.type") return native([](const Args& args) {
        need(args, 1, "mime.type");
        std::string path = lowerAscii(stringArg(args, 0, "mime.type"));
        const size_t query = path.find_first_of("?#"); if (query != std::string::npos) path.resize(query);
        const size_t slash = path.find_last_of("/\\"); if (slash != std::string::npos) path.erase(0, slash + 1);
        for (const auto& entry : mimeEntries()) {
            const std::string ext(entry.extension);
            if (path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0) return Value(std::string(entry.type));
        }
        return Value(std::string("application/octet-stream"));
    });
    if (name == "std:mime.extension") return native([](const Args& args) {
        need(args, 1, "mime.extension");
        const std::string type = lowerAscii(stringArg(args, 0, "mime.extension"));
        for (const auto& entry : mimeEntries()) if (type == entry.type) return Value(std::string(entry.extension));
        return Value(std::string{});
    });
    if (name == "std:mime.isText") return native([](const Args& args) {
        need(args, 1, "mime.isText");
        const std::string type = lowerAscii(stringArg(args, 0, "mime.isText"));
        if (type.rfind("text/", 0) == 0) return Value(true);
        return Value(type == "application/json" || (type.size() >= 5 && type.compare(type.size()-5,5,"+json")==0) ||
                     type == "application/xml" || (type.size() >= 4 && type.compare(type.size()-4,4,"+xml")==0) ||
                     type == "application/yaml" || type == "application/toml" || type == "application/javascript" ||
                     type == "text/javascript" || type == "image/svg+xml" || type == "application/x-www-form-urlencoded" ||
                     type == "application/graphql" || type == "application/sql");
    });

    if (name == "std:cookie.parse") return native([](const Args& args) {
        need(args, 1, "cookie.parse");
        const std::string input = stringArg(args, 0, "cookie.parse");
        if (input.size() > 1024u * 1024u) throw std::runtime_error("Cookie header exceeds 1 MiB safety limit");
        auto out = std::make_shared<Object>();
        size_t pos = 0;
        while (pos < input.size()) {
            while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == ';')) ++pos;
            if (pos >= input.size()) break;
            const size_t end = input.find(';', pos);
            const size_t stop = end == std::string::npos ? input.size() : end;
            const size_t equal = input.find('=', pos);
            if (equal == std::string::npos || equal >= stop) { pos = stop + (end == std::string::npos ? 0 : 1); continue; }
            size_t keyEnd = equal;
            while (keyEnd > pos && (input[keyEnd - 1] == ' ' || input[keyEnd - 1] == '\t')) --keyEnd;
            size_t valueStart = equal + 1;
            while (valueStart < stop && (input[valueStart] == ' ' || input[valueStart] == '\t')) ++valueStart;
            size_t valueEnd = stop;
            while (valueEnd > valueStart && (input[valueEnd - 1] == ' ' || input[valueEnd - 1] == '\t')) --valueEnd;
            std::string key = input.substr(pos, keyEnd - pos);
            std::string value = input.substr(valueStart, valueEnd - valueStart);
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
            if (!key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char c) { return cookieTokenChar(c); })) {
                value = cookieDecode(value);
                auto it = out->fields.find(key);
                if (it == out->fields.end()) out->fields.emplace(key, Value(value));
                else if (auto existing = std::get_if<std::shared_ptr<Array>>(&it->second.data)) (*existing)->items.emplace_back(value);
                else { auto duplicates = std::make_shared<Array>(); duplicates->items.push_back(it->second); duplicates->items.emplace_back(value); it->second = Value(duplicates); }
            }
            pos = stop + (end == std::string::npos ? 0 : 1);
        }
        return Value(out);
    });
    if (name == "std:cookie.serialize") return native([](const Args& args) {
        need(args, 2, "cookie.serialize");
        if (args.size() > 3) throw std::runtime_error("cookie.serialize accepts name, value and optional options object");
        const std::string key = stringArg(args, 0, "cookie.serialize");
        if (key.empty() || !std::all_of(key.begin(), key.end(), [](unsigned char c) { return cookieTokenChar(c); }))
            throw std::runtime_error("cookie.serialize received an invalid cookie name");
        std::string out = key + "=" + cookieEncode(stringArg(args, 1, "cookie.serialize"));
        if (args.size() < 3 || std::holds_alternative<Undefined>(args[2].data) || std::holds_alternative<Null>(args[2].data)) return Value(out);
        auto options = std::get_if<std::shared_ptr<Object>>(&args[2].data);
        if (!options || !*options) throw std::runtime_error("cookie.serialize options must be an object");
        auto textOption = [&](const char* field, const char* label) {
            auto it = (*options)->fields.find(field);
            if (it == (*options)->fields.end() || std::holds_alternative<Undefined>(it->second.data) || std::holds_alternative<Null>(it->second.data)) return std::string{};
            if (!std::holds_alternative<std::string>(it->second.data)) throw std::runtime_error(std::string("cookie.serialize option `") + label + "` must be a string");
            std::string value = std::get<std::string>(it->second.data); validateCookieAttribute(value, label); return value;
        };
        auto boolOption = [&](const char* field) {
            auto it = (*options)->fields.find(field);
            if (it == (*options)->fields.end()) return false;
            if (!std::holds_alternative<bool>(it->second.data)) throw std::runtime_error(std::string("cookie.serialize option `") + field + "` must be boolean");
            return std::get<bool>(it->second.data);
        };
        const std::string domain = textOption("domain", "domain");
        const std::string path = textOption("path", "path");
        const std::string expires = textOption("expires", "expires");
        const std::string sameSite = textOption("sameSite", "sameSite");
        if (!domain.empty()) out += "; Domain=" + domain;
        if (!path.empty()) out += "; Path=" + path;
        if (!expires.empty()) out += "; Expires=" + expires;
        auto age = (*options)->fields.find("maxAge");
        if (age != (*options)->fields.end()) {
            auto integer = std::get_if<int64_t>(&age->second.data);
            if (!integer) throw std::runtime_error("cookie.serialize option `maxAge` must be an integer");
            out += "; Max-Age=" + std::to_string(*integer);
        }
        if (boolOption("httpOnly")) out += "; HttpOnly";
        if (boolOption("secure")) out += "; Secure";
        if (!sameSite.empty()) {
            const std::string normalized = lowerAscii(sameSite);
            if (normalized != "strict" && normalized != "lax" && normalized != "none")
                throw std::runtime_error("cookie.serialize option `sameSite` must be Strict, Lax or None");
            if (normalized == "none" && !boolOption("secure"))
                throw std::runtime_error("SameSite=None cookies must also set Secure");
            out += std::string("; SameSite=") + (normalized == "strict" ? "Strict" : normalized == "lax" ? "Lax" : "None");
        }
        return Value(out);
    });
    return undefinedValue();
}
} // namespace astra
