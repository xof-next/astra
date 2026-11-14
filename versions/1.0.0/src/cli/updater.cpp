#include "astra/updater.hpp"
#include "astra/crypto.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#ifndef ASTRA_VERSION
#define ASTRA_VERSION "1.0.0"
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace astra { namespace fs = std::filesystem; namespace {
constexpr const char* kManifestUrl = "https://raw.githubusercontent.com/xof-next/astra/main/pack.json";
constexpr const char* kManifestHost = "https://raw.githubusercontent.com/xof-next/astra/";
constexpr const char* kReleaseApiPrefix = "https://api.github.com/repos/xof-next/astra/releases/tags/";
constexpr size_t kMaxDownload = 256u * 1024u * 1024u;
struct HttpResult { long status=0; std::string body; std::string error; std::string effectiveUrl; };
struct Sink { std::string* text=nullptr; std::ofstream* file=nullptr; size_t written=0; bool tooLarge=false; };
size_t writeCallback(char* data,size_t size,size_t count,void* opaque) {
    auto* sink=static_cast<Sink*>(opaque); const size_t bytes=size*count;
    if(bytes>kMaxDownload-sink->written){sink->tooLarge=true;return 0;}
    if(sink->text) sink->text->append(data,bytes);
    if(sink->file) sink->file->write(data,static_cast<std::streamsize>(bytes));
    sink->written+=bytes; return bytes;
}
HttpResult request(const std::string& url,std::string* outputFile=nullptr) {
    HttpResult result; CURL* curl=curl_easy_init(); if(!curl){result.error="Unable to initialize HTTPS client";return result;}
    std::ofstream file; if(outputFile){file.open(*outputFile,std::ios::binary|std::ios::trunc);if(!file){result.error="Cannot create temporary download file";curl_easy_cleanup(curl);return result;}}
    Sink sink; sink.text=outputFile?nullptr:&result.body; sink.file=outputFile?&file:nullptr;
    struct curl_slist* headers=nullptr;
    headers=curl_slist_append(headers,"Accept: application/vnd.github+json");
    headers=curl_slist_append(headers,"X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"Astra-Script-Runtime/" ASTRA_VERSION);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,180L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeCallback);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&sink);
    CURLcode rc=curl_easy_perform(curl);
    if(rc!=CURLE_OK) result.error=sink.tooLarge?"Download exceeded 256 MiB safety limit":curl_easy_strerror(rc);
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&result.status);
    char* effective=nullptr;curl_easy_getinfo(curl,CURLINFO_EFFECTIVE_URL,&effective);if(effective)result.effectiveUrl=effective;
    if(file.is_open()){file.flush();if(!file)result.error="Failed writing downloaded file";file.close();}
    curl_slist_free_all(headers);curl_easy_cleanup(curl);return result;
}
std::string jsonString(const std::string& source,const std::string& key) {
    const std::string needle="\""+key+"\"";
    size_t p=source.find(needle);
    if(p==std::string::npos)return {};
    p=source.find(':',p+needle.size());
    if(p==std::string::npos)return {};
    ++p;
    while(p<source.size()&&std::isspace(static_cast<unsigned char>(source[p])))++p;
    if(p>=source.size()||source[p]!='"')return {};
    std::string out;
    for(++p;p<source.size();++p){
        char c=source[p];
        if(c=='"')return out;
        if(c=='\\'&&p+1<source.size()){
            char e=source[++p];
            switch(e){case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;default:out+=e;}
        } else out+=c;
    }
    return {};
}
std::vector<std::string> jsonArrayObjects(const std::string& source,const std::string& key) {
    std::vector<std::string> out;const std::string needle="\""+key+"\"";size_t p=source.find(needle);if(p==std::string::npos)return out;
    p=source.find('[',p+needle.size());if(p==std::string::npos)return out;
    bool inString=false,escape=false;int depth=0;size_t start=0;
    for(++p;p<source.size();++p){char c=source[p];if(inString){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')inString=false;continue;}if(c=='"'){inString=true;continue;}if(c=='{'){if(depth++==0)start=p;}else if(c=='}'){if(--depth==0)out.push_back(source.substr(start,p-start+1));}else if(c==']'&&depth==0)break;}
    return out;
}
std::string jsonObjectValue(const std::string& source,const std::string& key) {
    const std::string needle="\""+key+"\""; size_t p=source.find(needle);
    if(p==std::string::npos)return {};
    p=source.find(':',p+needle.size()); if(p==std::string::npos)return {};
    while(++p<source.size()&&std::isspace(static_cast<unsigned char>(source[p]))){}
    if(p>=source.size()||source[p]!='{')return {};
    const size_t start=p; int depth=0; bool quoted=false,escaped=false;
    for(;p<source.size();++p){char c=source[p];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}if(c=='"'){quoted=true;continue;}if(c=='{')++depth;else if(c=='}'&&--depth==0)return source.substr(start,p-start+1);}
    return {};
}
std::vector<std::pair<std::string,std::string>> jsonObjectFields(const std::string& object) {
    std::vector<std::pair<std::string,std::string>> fields;if(object.size()<2||object.front()!='{')return fields;
    size_t p=1;while(p<object.size()){
        while(p<object.size()&&(std::isspace(static_cast<unsigned char>(object[p]))||object[p]==','))++p;
        if(p>=object.size()||object[p]=='}')break;
        if(object[p]!='"')return {};
        const size_t keyStart=++p;bool escaped=false;
        for(;p<object.size();++p){if(escaped)escaped=false;else if(object[p]=='\\')escaped=true;else if(object[p]=='"')break;}
        if(p>=object.size())return {};
        std::string key=object.substr(keyStart,p-keyStart);++p;
        while(p<object.size()&&std::isspace(static_cast<unsigned char>(object[p])))++p;
        if(p>=object.size()||object[p++]!=':')return {};
        while(p<object.size()&&std::isspace(static_cast<unsigned char>(object[p])))++p;
        const size_t valueStart=p;bool quoted=false,escape=false;int depth=0;
        if(p<object.size()&&(object[p]=='{'||object[p]=='[')){
            const char open=object[p],close=open=='{'?'}':']';
            for(;p<object.size();++p){char c=object[p];if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;continue;}if(c=='"'){quoted=true;continue;}if(c==open)++depth;else if(c==close&&--depth==0){++p;break;}}
        }else if(p<object.size()&&object[p]=='"'){
            quoted=true;for(++p;p<object.size();++p){char c=object[p];if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"'){++p;break;}}
        }else{while(p<object.size()&&object[p]!=','&&object[p]!='}')++p;}
        fields.emplace_back(std::move(key),object.substr(valueStart,p-valueStart));
    }
    return fields;
}
bool validVersion(const std::string& value) {
    if(value.empty()||value.size()>64)return false;
    size_t i=0;int dots=0;bool digit=false;
    for(;i<value.size()&&value[i]!='-';++i){char c=value[i];if(c=='.'){if(!digit)return false;++dots;digit=false;}else if(c>='0'&&c<='9')digit=true;else return false;}
    if(!digit||dots!=2)return false;
    if(i<value.size()){++i;if(i>=value.size())return false;for(;i<value.size();++i){char c=value[i];if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='.'||c=='-'))return false;}}
    return true;
}
std::string withoutV(std::string s);
std::string versionTag(const std::string& metadata,const std::string& version) {
    std::string tag=jsonString(metadata,"tag");if(tag.empty())tag="v"+version;
    const std::string normalized=withoutV(tag);return validVersion(normalized)?tag:std::string{};
}
HttpResult loadPackManifest() {
    // During development, prefer the checked-out repository manifest only when
    // the current directory is an actual Astra version source tree.
    std::error_code ec;fs::path cursor=fs::current_path(ec);
    if(!ec&&fs::is_regular_file(cursor/"CMakeLists.txt",ec)&&!ec&&
       fs::is_regular_file(cursor/"src/cli/main.cpp",ec)&&!ec&&
       fs::is_regular_file(cursor/"stdlib/modules.toml",ec)&&!ec) {
        fs::path root=cursor;
        for(int depth=0;depth<4&&!root.empty();++depth,root=root.parent_path()) {
            const fs::path manifestPath=root/"pack.json";
            const fs::path versionSource=root/"versions"/currentVersion()/"CMakeLists.txt";
            ec.clear();
            if(fs::is_regular_file(manifestPath,ec)&&!ec&&fs::is_regular_file(versionSource,ec)&&!ec) {
                const auto size=fs::file_size(manifestPath,ec);
                if(!ec&&size<=1024u*1024u) {
                    std::ifstream input(manifestPath,std::ios::binary);std::ostringstream content;content<<input.rdbuf();
                    if(input.good()||input.eof()) {HttpResult local;local.status=200;local.body=content.str();local.effectiveUrl=kManifestHost;return local;}
                }
            }
        }
    }
    HttpResult result=request(kManifestUrl);
    if(result.status>=200&&result.status<300&&result.effectiveUrl.rfind(kManifestHost,0)!=0){result.error="Manifest redirected to an untrusted host";}
    return result;
}
std::string versionMetadata(const std::string& manifest,const std::string& version) {
    if(!validVersion(version))return {};
    const auto versions=jsonObjectValue(manifest,"versions");
    for(const auto& field:jsonObjectFields(versions))if(field.first==version&& !field.second.empty()&&field.second.front()=='{')return field.second;
    return {};
}
fs::path versionBinaryPath(const std::string& version) {
    if(!validVersion(version))return {};
#ifdef _WIN32
    const char* base=std::getenv("LOCALAPPDATA");if(!base||!*base)return {};
    return fs::path(base)/"Astra"/"versions"/version/"astra.exe";
#else
    const char* home=std::getenv("HOME");if(!home||!*home)return {};
    return fs::path(home)/".astra"/"versions"/version/"astra";
#endif
}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
bool approvedDownloadHost(const std::string& url) {
    return url.rfind("https://github.com/",0)==0 ||
           url.rfind("https://release-assets.githubusercontent.com/",0)==0 ||
           url.rfind("https://objects.githubusercontent.com/",0)==0 ||
           url.rfind("https://github-releases.githubusercontent.com/",0)==0;
}
std::string withoutV(std::string s){if(!s.empty()&&(s[0]=='v'||s[0]=='V'))s.erase(s.begin());return s;}
std::string platformKey(){
#if defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
    return "windows-aarch64";
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "windows-x86_64";
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64))
    return "macos-aarch64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "macos-x86_64";
#elif defined(__linux__) && (defined(__aarch64__) || defined(_M_ARM64))
    return "linux-aarch64";
#elif defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    return "linux-x86_64";
#else
    return {};
#endif
}
std::string homeDirectory(){
#ifdef _WIN32
    const char* p=std::getenv("LOCALAPPDATA");if(p&&*p)return p;
#else
    const char* p=std::getenv("HOME");if(p&&*p)return p;
#endif
    return {};
}
fs::path targetPath(){const auto home=homeDirectory();if(home.empty())return {};
#ifdef _WIN32
    return fs::path(home)/"Programs"/"Astra"/"bin"/"astra.exe";
#else
    return fs::path(home)/".local"/"bin"/"astra";
#endif
}
bool isPlatformBinary(const fs::path& path){std::ifstream f(path,std::ios::binary);unsigned char b[4]={};f.read(reinterpret_cast<char*>(b),4);if(f.gcount()<2)return false;
#ifdef _WIN32
    return b[0]=='M'&&b[1]=='Z';
#elif defined(__APPLE__)
    return (b[0]==0xcf&&b[1]==0xfa)||(b[0]==0xfe&&b[1]==0xed)||(b[0]==0xca&&b[1]==0xfe);
#else
    return b[0]==0x7f&&b[1]=='E'&&b[2]=='L'&&b[3]=='F';
#endif
}
bool installBinary(const fs::path& downloaded,const fs::path& target,std::string& error){
    std::error_code ec;fs::create_directories(target.parent_path(),ec);if(ec){error="Cannot create runtime directory: "+ec.message();return false;}
#ifdef _WIN32
    fs::path staged=target;staged += ".new";fs::copy_file(downloaded,staged,fs::copy_options::overwrite_existing,ec);if(ec){error="Cannot stage new runtime: "+ec.message();return false;}
    // Windows cannot replace an executable while it is mapped. The staged image is picked up by a small detached helper after this process exits.
    fs::path script=target;script += ".update.cmd";std::ofstream out(script);if(!out){error="Cannot create update helper";return false;}
    auto q=[](std::string x){size_t p=0;while((p=x.find('"',p))!=std::string::npos){x.insert(p,"^");p+=2;}return x;};
    out<<"@echo off\r\nsetlocal\r\nfor /l %%i in (1,1,60) do (\r\n  timeout /t 1 /nobreak >nul\r\n  move /y \""<<q(staged.string())<<"\" \""<<q(target.string())<<"\" >nul 2>nul && goto done\r\n)\r\nexit /b 1\r\n:done\r\ndel \"%~f0\"\r\n";out.close();
    std::wstring cmd=L"cmd.exe /c \"";std::string scriptS=script.string();int wlen=MultiByteToWideChar(CP_UTF8,0,scriptS.c_str(),-1,nullptr,0);std::wstring scriptW(static_cast<size_t>(wlen),L'\0');MultiByteToWideChar(CP_UTF8,0,scriptS.c_str(),-1,scriptW.data(),wlen);if(!scriptW.empty()&&scriptW.back()==L'\0')scriptW.pop_back();cmd+=scriptW;cmd+=L"\"";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end());mutableCmd.push_back(L'\0');if(!CreateProcessW(nullptr,mutableCmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|DETACHED_PROCESS,nullptr,nullptr,&si,&pi)){error="Runtime staged at "+staged.string()+" but update helper could not start";return false;}CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;
#else
    fs::path temp=target;temp += ".new";fs::copy_file(downloaded,temp,fs::copy_options::overwrite_existing,ec);if(ec){error="Cannot stage new runtime: "+ec.message();return false;}chmod(temp.c_str(),0755);fs::rename(temp,target,ec);if(ec){fs::remove(temp);error="Cannot atomically replace runtime: "+ec.message();return false;}return true;
#endif
}
bool storeVersionBinary(const fs::path& downloaded,const fs::path& target,std::string& error) {
    std::error_code ec;fs::create_directories(target.parent_path(),ec);if(ec){error="Cannot create version directory: "+ec.message();return false;}
    fs::path staged=target;staged += ".new";fs::copy_file(downloaded,staged,fs::copy_options::overwrite_existing,ec);if(ec){error="Cannot stage version binary: "+ec.message();return false;}
#ifndef _WIN32
    chmod(staged.c_str(),0755);
#endif
#ifdef _WIN32
    fs::remove(target,ec);ec.clear();
#endif
    fs::rename(staged,target,ec);if(ec){fs::remove(staged);error="Cannot activate version binary: "+ec.message();return false;}return true;
}
std::string checksumFromList(const std::string& list,const std::string& name){std::istringstream in(list);std::string line;while(std::getline(in,line)){line=trim(line);if(line.empty())continue;std::istringstream row(line);std::string hash,file;row>>hash>>file;if(!file.empty()&&file[0]=='*')file.erase(file.begin());if(file==name&&hash.size()==64)return hash;}return {};}
int fail(const std::string& code,const std::string& message){std::cerr<<"error["<<code<<"]: "<<message<<"\n";return 1;}
} // namespace
std::string currentVersion(){return ASTRA_VERSION;}
std::string installedRuntimePath(){return targetPath().string();}
int installBuiltRuntime(const std::string& sourcePath) {
    const fs::path source=fs::u8path(sourcePath);if(!isPlatformBinary(source))return fail("E9020","The selected file is not a native Astra executable");
    const auto versionPath=versionBinaryPath(currentVersion());if(versionPath.empty())return fail("E9008","Cannot determine user version storage directory");
    std::string error;if(!storeVersionBinary(source,versionPath,error))return fail("E9021",error);
    const auto target=targetPath();if(target.empty())return fail("E9008","Cannot determine user installation directory");
    if(!installBinary(versionPath,target,error))return fail("E9022",error);
    std::cout<<"Astra "<<currentVersion()<<" installed globally.\nVersion: "<<versionPath.string()<<"\nExecutable: "<<target.string()<<"\n";
#ifdef _WIN32
    std::cout<<"Add this directory to your user PATH, then open a new terminal:\n  "<<target.parent_path().string()<<"\n";
#else
    const char* path=std::getenv("PATH");const std::string bin=target.parent_path().string();if(!path||std::string(path).find(bin)==std::string::npos)std::cout<<"Add this line to your shell profile if needed:\n  export PATH=\"$HOME/.local/bin:$PATH\"\n";
#endif
    std::cout<<"Verify with: astra --version\n";return 0;
}
int installRuntimeVersion(const std::string& requestedVersion,bool update) {
    const auto target=targetPath();if(target.empty())return fail("E9008","Cannot determine user installation directory");
    const std::string platform=platformKey();if(platform.empty())return fail("E9009","Unsupported operating system or CPU architecture");
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)return fail("E9010","Unable to initialize HTTPS client");
    auto manifestResponse=loadPackManifest();
    if(!manifestResponse.error.empty()||manifestResponse.status<200||manifestResponse.status>=300){curl_global_cleanup();return fail("E9010","Cannot load Astra pack.json: "+(manifestResponse.error.empty()?"HTTP "+std::to_string(manifestResponse.status):manifestResponse.error));}
    if(jsonString(manifestResponse.body,"name")!="astra"){curl_global_cleanup();return fail("E9011","Invalid pack.json: expected package name `astra`");}
    const std::string selected=requestedVersion.empty()?jsonString(manifestResponse.body,"new"):withoutV(requestedVersion);
    if(!validVersion(selected)){curl_global_cleanup();return fail("E9011","Invalid or missing version in pack.json");}
    const std::string metadata=versionMetadata(manifestResponse.body,selected);
    if(metadata.empty()){curl_global_cleanup();return fail("E9011","Astra version "+selected+" is not listed in pack.json");}
    const std::string status=jsonString(metadata,"status");if(status=="yanked"||status=="disabled"){curl_global_cleanup();return fail("E9011","Astra version "+selected+" is not available for installation");}
    const std::string tag=versionTag(metadata,selected);if(tag.empty()){curl_global_cleanup();return fail("E9011","Invalid release tag for Astra "+selected);}
    const std::string platformAsset=jsonString(jsonObjectValue(metadata,"assets"),platform);
    if(platformAsset.empty()||platformAsset.size()>128||platformAsset.find('/')!=std::string::npos||platformAsset.find('\\')!=std::string::npos){curl_global_cleanup();return fail("E9011","pack.json has no safe binary asset for platform `"+platform+"`");}
    const auto versionPath=versionBinaryPath(selected);if(versionPath.empty()){curl_global_cleanup();return fail("E9008","Cannot determine user version storage directory");}
    std::error_code existsError;const bool alreadyInstalled=fs::is_regular_file(versionPath,existsError)&&!existsError&&isPlatformBinary(versionPath);
    if(!alreadyInstalled){
        const std::string releaseUrl=std::string(kReleaseApiPrefix)+tag;
        auto release=request(releaseUrl);
        if(!release.error.empty()||release.status<200||release.status>=300){curl_global_cleanup();return fail("E9012",release.status==404?"Release "+tag+" is not published yet. Publish its platform binaries first.":"Cannot load release "+tag+": "+(release.error.empty()?"HTTP "+std::to_string(release.status):release.error));}
        std::string downloadUrl,digest;for(const auto& object:jsonArrayObjects(release.body,"assets")){if(jsonString(object,"name")==platformAsset){downloadUrl=jsonString(object,"browser_download_url");digest=jsonString(object,"digest");break;}}
        if(downloadUrl.empty()){curl_global_cleanup();return fail("E9012","Release "+tag+" does not contain platform asset `"+platformAsset+"`");}
        if(downloadUrl.rfind("https://github.com/xof-next/astra/releases/download/",0)!=0){curl_global_cleanup();return fail("E9013","Release asset URL is not an approved xof-next/astra HTTPS URL");}
        std::error_code ec;fs::create_directories(versionPath.parent_path(),ec);if(ec){curl_global_cleanup();return fail("E9017","Cannot create version directory: "+ec.message());}
        fs::path temp=versionPath;temp += ".download";std::string tempName=temp.string();auto bin=request(downloadUrl,&tempName);
        if(!bin.error.empty()||bin.status<200||bin.status>=300||!approvedDownloadHost(bin.effectiveUrl)){fs::remove(temp,ec);curl_global_cleanup();return fail("E9014","Runtime download failed or redirected to an untrusted host: "+(bin.error.empty()?"HTTP "+std::to_string(bin.status):bin.error));}
        std::string expected;if(digest.rfind("sha256:",0)==0)expected=digest.substr(7);
        if(expected.empty()){
            std::string checksumUrl;for(const auto& assetObject:jsonArrayObjects(release.body,"assets")){const auto name=jsonString(assetObject,"name");if(name=="SHA256SUMS.txt"||name=="SHA256SUMS"){checksumUrl=jsonString(assetObject,"browser_download_url");break;}}
            if(checksumUrl.rfind("https://github.com/xof-next/astra/releases/download/",0)==0){auto sum=request(checksumUrl);if(sum.status>=200&&sum.status<300&&approvedDownloadHost(sum.effectiveUrl))expected=checksumFromList(sum.body,platformAsset);}
        }
        if(expected.size()!=64){fs::remove(temp,ec);curl_global_cleanup();return fail("E9015","Release does not provide a SHA-256 digest for `"+platformAsset+"`; refusing an unverified runtime");}
        std::ifstream input(temp,std::ios::binary);std::ostringstream bytes;bytes<<input.rdbuf();if(!input.good()&&!input.eof()){fs::remove(temp,ec);curl_global_cleanup();return fail("E9014","Cannot read downloaded runtime for verification");}
        std::transform(expected.begin(),expected.end(),expected.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});const auto actual=sha256Hex(bytes.str());if(!constantTimeEqual(actual,expected)){fs::remove(temp,ec);curl_global_cleanup();return fail("E9015","SHA-256 verification failed; downloaded runtime was discarded");}
        if(!isPlatformBinary(temp)){fs::remove(temp,ec);curl_global_cleanup();return fail("E9016","Downloaded asset is not a valid native executable for this platform");}
        std::string error;if(!storeVersionBinary(temp,versionPath,error)){fs::remove(temp,ec);curl_global_cleanup();return fail("E9017",error);}fs::remove(temp,ec);
    }
    std::string activationError;if(!installBinary(versionPath,target,activationError)){curl_global_cleanup();return fail("E9017",activationError);}
    curl_global_cleanup();std::cout<<"Astra "<<(alreadyInstalled?(update?"selected":"already installed; activated"):"installed")<<" and activated.\nVersion: "<<selected<<"\nRuntime: "<<target.string()<<"\n";
#ifndef _WIN32
    if(target.parent_path().string().find(".local/bin")!=std::string::npos){const char* path=std::getenv("PATH");if(!path||std::string(path).find(target.parent_path().string())==std::string::npos)std::cout<<"Ensure ~/.local/bin is in PATH.\n";}
#endif
    return 0;
}
int useInstalledVersion(const std::string& requestedVersion) {
    const std::string version=withoutV(requestedVersion);if(!validVersion(version))return fail("E9023","Invalid version. Expected semantic version, for example 1.0.0");
    const auto source=versionBinaryPath(version);if(source.empty())return fail("E9008","Cannot determine user version storage directory");
    if(!isPlatformBinary(source))return fail("E9024","Astra "+version+" is not installed. Run `astra install "+version+"` first.");
    const auto target=targetPath();if(target.empty())return fail("E9008","Cannot determine user installation directory");
    std::string error;if(!installBinary(source,target,error))return fail("E9022",error);
    std::cout<<"Now using Astra "<<version<<".\nRuntime: "<<target.string()<<"\n";return 0;
}
int listAvailableVersions() {
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)return fail("E9010","Unable to initialize HTTPS client");
    auto response=loadPackManifest();if(!response.error.empty()||response.status<200||response.status>=300){curl_global_cleanup();return fail("E9010","Cannot load Astra pack.json: "+(response.error.empty()?"HTTP "+std::to_string(response.status):response.error));}
    const std::string latest=jsonString(response.body,"new");const auto versions=jsonObjectValue(response.body,"versions");if(versions.empty()){curl_global_cleanup();return fail("E9011","Invalid pack.json: missing versions object");}
    std::cout<<"Astra versions (new: "<<latest<<")\n";
    for(const auto& field:jsonObjectFields(versions)){
        if(!validVersion(field.first))continue;
        const std::string status=jsonString(field.second,"status");const auto installed=versionBinaryPath(field.first);std::error_code ec;const bool present=fs::is_regular_file(installed,ec)&&!ec&&isPlatformBinary(installed);
        std::cout<<(field.first==latest?"* ":"  ")<<field.first<<"  "<<(status.empty()?"unknown":status)<<(present?"  [installed]":"")<<(field.first==currentVersion()?"  [running build]":"")<<"\n";
    }
    curl_global_cleanup();return 0;
}
} // namespace astra
