#include "astra/modules.hpp"
namespace astra {
const std::vector<Module>& builtinModules(){static const std::vector<Module> m={
 {"std:crypto",{"sha256","hmacSha256","timingSafeEqual"}},
 {"std:object",{"keys","values","entries","has","assign","remove"}},
 {"std:date",{"now","toISOString","year","month","day"}},
 {"std:math",{"abs","ceil","floor","round","sqrt","pow","min","max","clamp","sign","random"}},
 {"std:string",{"upper","lower","trim","contains","startsWith","endsWith","replace","split","repeat","reverse"}},
 {"std:array",{"length","push","pop","contains","join","reverse","sort"}},
 {"std:json",{"parse","stringify"}},
 {"std:time",{"now","sleep","measure"}},
 {"std:encoding",{"base64Encode","base64Decode","hexEncode","hexDecode","urlEncode","urlDecode"}},
 {"std:fs",{"readText","writeText","appendText","exists","remove","mkdir","list"}},
 {"std:path",{"join","basename","dirname","extension","normalize","isAbsolute"}},
 {"std:env",{"get","has","set"}},
 {"std:process",{"cwd","platform"}},
 {"std:random",{"int","float","bool","choice"}},
 {"std:uuid",{"v4"}},
 {"std:regex",{"test","match","replace"}},
 {"std:testing",{"assert","equal","notEqual"}},
 {"std:url",{"encode","decode","parse"}},
 {"std:csv",{"parse","stringify"}},
 {"std:console",{"log","info","warn","error","debug","table","clear"}},
 {"std:system",{"platform","arch","eol","cwd"}},
 {"std:format",{"padStart","padEnd","truncate","capitalize","camelCase","snakeCase","kebabCase"}},
 {"std:statistics",{"sum","mean","median","min","max","variance","standardDeviation"}},
 {"std:launch",{"request","get","post","put","patch","del","delete","head","options"}},
 {"std:server",{"create"}},
 {"std:dns",{"lookup","resolve","reverse","isIP"}},
 {"std:tcp",{"connect"}},
 {"std:tls",{"backend","supported","policy","verify"}},
 {"std:events",{"on","once","off","emit","listenerCount","removeAllListeners","eventNames"}},
 {"std:buffer",{"byteLength","concat","slice","toHex","fromHex","toBase64","fromBase64"}},
 {"std:compress",{"gzip","gunzip","deflate","inflate","crc32"}},
 {"std:query",{"parse","stringify","get","append"}},
 {"std:validate",{"isEmail","isUUID","isHex","isIPv4","isURL","isEmpty"}},
 {"std:html",{"escape","unescape"}},
 {"std:mime",{"type","extension","isText"}},
 {"std:cookie",{"parse","serialize"}},
 {"std:collections",{"unique","chunk","flatten","zip","groupBy"}}
};return m;}
bool hasModule(const std::string& n){for(const auto& m:builtinModules())if(m.name==n)return true;return false;}
bool hasExport(const std::string& n,const std::string& e){for(const auto& m:builtinModules())if(m.name==n)for(const auto& x:m.exports)if(x==e)return true;return false;}
}
