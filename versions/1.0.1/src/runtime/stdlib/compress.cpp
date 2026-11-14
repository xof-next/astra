#include "astra/advanced_stdlib.hpp"
#include <zlib.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

namespace astra { namespace {
using Args = std::vector<Value>;
Value undefinedValue() { return Value{}; }
Value native(NativeFunction f) { return Value(std::move(f)); }
void need(const Args& a, size_t n, const std::string& fn) { if (a.size() < n) throw std::runtime_error(fn + " expects at least " + std::to_string(n) + " argument(s)"); }
std::string stringArg(const Args& a, size_t i, const std::string& fn) { if (i >= a.size() || !std::holds_alternative<std::string>(a[i].data)) throw std::runtime_error(fn + " expects a string"); return std::get<std::string>(a[i].data); }
constexpr size_t kMaxOutput = 64u * 1024u * 1024u;
std::string compressData(const std::string& input, bool gzip) {
    if (input.size() > kMaxOutput) throw std::runtime_error("Compression input exceeds 64 MiB safety limit");
    z_stream stream{};
    const int windowBits = gzip ? MAX_WBITS + 16 : MAX_WBITS;
    int rc = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY);
    if (rc != Z_OK) throw std::runtime_error("Compression initialization failed");
    struct Guard { z_stream* s; ~Guard(){ deflateEnd(s); } } guard{&stream};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    if (input.size() > std::numeric_limits<uInt>::max()) throw std::runtime_error("Input exceeds zlib's supported single-buffer size");
    stream.avail_in = static_cast<uInt>(input.size());
    std::string output;
    char chunk[16384];
    int flush = Z_FINISH;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(chunk);
        stream.avail_out = sizeof(chunk);
        rc = deflate(&stream, flush);
        if (rc != Z_OK && rc != Z_STREAM_END) throw std::runtime_error("Compression failed");
        output.append(chunk, sizeof(chunk) - stream.avail_out);
        if (output.size() > kMaxOutput) throw std::runtime_error("Compressed output exceeds 64 MiB");
    } while (rc != Z_STREAM_END);
    return output;
}
std::string decompressData(const std::string& input, bool gzip) {
    if (input.size() > std::numeric_limits<uInt>::max()) throw std::runtime_error("Input exceeds zlib's supported single-buffer size");
    z_stream stream{};
    const int windowBits = gzip ? MAX_WBITS + 16 : MAX_WBITS;
    int rc = inflateInit2(&stream, windowBits);
    if (rc != Z_OK) throw std::runtime_error("Decompression initialization failed");
    struct Guard { z_stream* s; ~Guard(){ inflateEnd(s); } } guard{&stream};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::string output;
    char chunk[16384];
    do {
        const uLong beforeIn = stream.total_in;
        const uLong beforeOut = stream.total_out;
        stream.next_out = reinterpret_cast<Bytef*>(chunk);
        stream.avail_out = sizeof(chunk);
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) throw std::runtime_error("Invalid or corrupted compressed data");
        output.append(chunk, sizeof(chunk) - stream.avail_out);
        if (output.size() > kMaxOutput) throw std::runtime_error("Decompressed output exceeds 64 MiB safety limit");
        if (rc == Z_BUF_ERROR && (stream.avail_in == 0 || (stream.total_in == beforeIn && stream.total_out == beforeOut)))
            throw std::runtime_error("Compressed data is truncated or cannot make progress");
    } while (rc != Z_STREAM_END);
    if (stream.avail_in != 0) throw std::runtime_error("Trailing data after compressed stream");
    return output;
}
}
Value createCompressionBuiltin(const std::string& name) {
    if (name == "std:compress.gzip") return native([](const Args& a){need(a,1,"compress.gzip");return Value(compressData(stringArg(a,0,"compress.gzip"),true));});
    if (name == "std:compress.gunzip") return native([](const Args& a){need(a,1,"compress.gunzip");return Value(decompressData(stringArg(a,0,"compress.gunzip"),true));});
    if (name == "std:compress.deflate") return native([](const Args& a){need(a,1,"compress.deflate");return Value(compressData(stringArg(a,0,"compress.deflate"),false));});
    if (name == "std:compress.inflate") return native([](const Args& a){need(a,1,"compress.inflate");return Value(decompressData(stringArg(a,0,"compress.inflate"),false));});
    if (name == "std:compress.crc32") return native([](const Args& a){need(a,1,"compress.crc32");const auto s=stringArg(a,0,"compress.crc32");uLong crc=crc32(0L,Z_NULL,0);crc=crc32(crc,reinterpret_cast<const Bytef*>(s.data()),static_cast<uInt>(s.size()));return Value(static_cast<int64_t>(crc));});
    return undefinedValue();
}
} // namespace astra
