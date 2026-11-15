#include "astra/checker.hpp"
#include "astra/interpreter.hpp"
#include "astra/modules.hpp"
#include "astra/runtime_config.hpp"
#include "astra/updater.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
  #include <limits.h>
  #include <unistd.h>
#else
  #include <limits.h>
  #include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}
std::string unquote(std::string s) {
    s = trim(std::move(s));
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) return s.substr(1, s.size() - 2);
    return {};
}
std::string manifestEntry() {
    std::ifstream f("Astra.toml"); if (!f) return {};
    std::string line; bool project = false;
    while (std::getline(f, line)) {
        line = trim(line); if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { project = line == "[project]"; continue; }
        if (project && line.rfind("entry", 0) == 0) { const auto equal = line.find('='); if (equal != std::string::npos) return unquote(line.substr(equal + 1)); }
    }
    return {};
}
std::map<std::string, std::string> manifestScripts() {
    std::map<std::string, std::string> scripts;
    std::ifstream file("Astra.toml");
    if (!file) return scripts;
    std::string line;
    bool inScripts = false;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            inScripts = line == "[scripts]";
            continue;
        }
        if (!inScripts) continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const std::string name = trim(line.substr(0, equal));
        const std::string target = unquote(line.substr(equal + 1));
        if (!name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; }) && !target.empty()) scripts[name] = target;
    }
    return scripts;
}
bool resolveSource(const std::string& requested, fs::path& resolved) {
    if (requested.empty()) return false;
    std::error_code ec; fs::path candidate = fs::u8path(requested);
    if (fs::is_regular_file(candidate, ec) && !ec) { resolved = candidate; return true; }
    ec.clear(); if (!candidate.has_extension()) candidate += ".astra";
    if (fs::is_regular_file(candidate, ec) && !ec) { resolved = candidate; return true; }
    return false;
}
bool sourceCheckout(const fs::path& cwd) {
    return fs::is_regular_file(cwd / "CMakeLists.txt") && fs::is_regular_file(cwd / "src/cli/main.cpp") && fs::is_regular_file(cwd / "stdlib/modules.toml");
}
fs::path executablePath(const char* argv0) {
#if defined(_WIN32)
    std::vector<char> b(32768); DWORD n = GetModuleFileNameA(nullptr, b.data(), static_cast<DWORD>(b.size()));
    if (n > 0 && n < b.size()) return fs::path(std::string(b.data(), n));
#elif defined(__APPLE__)
    uint32_t n = 0; _NSGetExecutablePath(nullptr, &n);
    if (n > 0) { std::vector<char> b(n + 1); if (_NSGetExecutablePath(b.data(), &n) == 0) { std::error_code ec; auto p = fs::canonical(b.data(), ec); if (!ec) return p; } }
#else
    std::vector<char> b(PATH_MAX + 1); const ssize_t n = readlink("/proc/self/exe", b.data(), PATH_MAX);
    if (n > 0) return fs::path(std::string(b.data(), static_cast<size_t>(n)));
#endif
    std::error_code ec; auto p = fs::absolute(fs::u8path(argv0), ec); return ec ? fs::path(argv0) : p;
}
int installCurrentExecutable(const char* argv0) {
    const fs::path source = executablePath(argv0); std::error_code ec;
    if (!fs::is_regular_file(source, ec) || ec) {
        std::cerr << "error[E9101]: Cannot locate the running Astra executable.\n"; return 1;
    }
    return astra::installBuiltRuntime(source.string());
}
int initProject(const fs::path& directory) {
    std::error_code ec; fs::create_directories(directory, ec);
    if (ec) { std::cerr << "error[E9002]: Cannot create project directory: " << ec.message() << "\n"; return 1; }
    if (fs::exists(directory / "Astra.toml") || fs::exists(directory / "main.astra") || fs::exists(directory / "astra.conf")) { std::cerr << "error[E9005]: Astra project files already exist in " << directory.string() << "\n"; return 1; }
    std::ofstream manifest(directory / "Astra.toml", std::ios::binary);
    if (!manifest) { std::cerr << "error[E9002]: Cannot create Astra.toml\n"; return 1; }
    manifest << "[project]\nname = \"my-project\"\nversion = \"1.0.0\"\nentry = \"main.astra\"\n\n[dependencies]\n\n[devDependencies]\n\n[scripts]\ntest = \"lib/test.astra\"\n";
    manifest.close();
    std::ofstream runtimeConfig(directory / "astra.conf", std::ios::binary);
    if (!runtimeConfig) { std::cerr << "error[E9002]: Cannot create astra.conf\n"; return 1; }
    runtimeConfig << R"ASTRA([server]
host = "127.0.0.1"
workers = 0
queue_capacity = 512
backlog = 256
max_connections = 1024
max_connections_per_ip = 64
max_request_buffer = 1mb
max_response_buffer = 4mb
max_header_buffer = 64kb
request_timeout = 10s
max_tracked_ips = 16384

[rate_limit]
enabled = true
window = 1s
max_requests_total = 5000
max_requests_per_ip = 2000

[client]
max_request_buffer = 10mb
max_response_buffer = 10mb
max_header_buffer = 256kb
timeout = 15s
connect_timeout = 5s
max_redirects = 5
verify_tls = true
)ASTRA";
    runtimeConfig.close();
    fs::create_directories(directory / "lib", ec);
    if (ec) { std::cerr << "error[E9002]: Cannot create lib directory: " << ec.message() << "\n"; return 1; }
    std::ofstream mainFile(directory / "main.astra", std::ios::binary), testFile(directory / "lib/test.astra", std::ios::binary), namedTestFile(directory / "test.astra", std::ios::binary);
    if (!mainFile || !testFile || !namedTestFile) { std::cerr << "error[E9002]: Failed to write project source files\n"; return 1; }
    mainFile << "pub fn main() {\n    console.log(\"Hello from Astra!\");\n}\n";
    testFile << "pub fn main() {\n    assert(1 + 1 == 2, \"basic arithmetic test failed\");\n    console.log(\"Tests passed\");\n}\n";
    namedTestFile << "pub fn main() {\n    assert(2 * 3 == 6, \"named source test failed\");\n    console.log(\"test.astra executed\");\n}\n";
    std::cout << "Created Astra project at " << directory.string() << "\nFiles: Astra.toml, astra.conf, main.astra, test.astra, lib/test.astra\nRun: astra run\nTask: astra run test\nFile: astra file test.astra\n"; return 0;
}
bool resolveScriptTarget(const std::string& target, fs::path& source) {
    const std::string value = trim(target);
    if (value.empty() || value.find_first_of(" \t\r\n;&|><") != std::string::npos) return false;
    return resolveSource(value, source);
}

void help() {
    std::cout << "Astra v" << astra::currentVersion() << "\n\nUsage:\n"
              << "  astra                         Start the interactive REPL\n"
              << "  astra run [script|file]       Run a project script or source file\n"
              << "  astra file <file.astra>       Run a source file\n"
              << "  astra <file.astra>            Run a source file directly\n"
              << "  astra check <file>            Run static source checks\n"
              << "  astra modules                 List built-in standard modules\n"
              << "  astra config check            Validate astra.conf\n"
              << "  astra init                    Install a local build or create a project\n"
              << "  astra init --project [dir]    Create a project in the current/target directory\n"
              << "  astra install [version]       Install a version from pack.json\n"
              << "  astra update                  Install and activate the newest version\n"
              << "  astra versions                List available and installed versions\n"
              << "  astra use <version>           Activate an installed version\n"
              << "  astra --version               Show the runtime version\n"
              << "  astra --help                  Show this help\n\n"
              << "Aliases: i=install, u=update, r=run, c=check, v=versions\n"
              << "Project scripts are declared in [scripts] in Astra.toml.\n"
              << "REPL commands: .help, .version, .clear, .exit\n";
}
}
int main(int argc, char** argv) {
    using namespace astra;
    if (argc < 2) return runRepl();

    std::string command = argv[1];
    if (command == "i") command = "install";
    else if (command == "u") command = "update";
    else if (command == "r") command = "run";
    else if (command == "c") command = "check";
    else if (command == "v") command = "versions";

    if (command == "--version" || command == "-v") {
        std::cout << "Astra " << currentVersion() << "\n";
        return 0;
    }
    if (command == "version") {
        std::cout << "Astra " << currentVersion() << "\nRuntime: native C++17 interpreter\nGlobal install path: " << installedRuntimePath() << "\n";
        return 0;
    }
    if (command == "install") return installRuntimeVersion(argc > 2 ? argv[2] : "", false);
    if (command == "update") return installRuntimeVersion("", true);
    if (command == "versions") return listAvailableVersions();
    if (command == "use") {
        if (argc < 3) {
            std::cerr << "error[E9023]: Missing version.\nhelp: astra use <version>\n";
            return 2;
        }
        return useInstalledVersion(argv[2]);
    }
    if (command == "--help" || command == "help") {
        help();
        return 0;
    }
    if (command == "modules") {
        size_t count = 0;
        for (const auto& module : builtinModules()) {
            count += module.exports.size();
            std::cout << module.name << " (" << module.exports.size() << ")\n  ";
            for (size_t i = 0; i < module.exports.size(); ++i) std::cout << (i ? ", " : "") << module.exports[i];
            std::cout << "\n";
        }
        std::cout << "Total: " << builtinModules().size() << " standard modules, " << count << " exports\n";
        return 0;
    }
    if (command == "config") {
        if (argc != 3 || std::string(argv[2]) != "check") {
            std::cerr << "Usage: astra config check\n";
            return 2;
        }
        try {
            const RuntimeConfig& config = RuntimeConfig::instance();
            if (config.loadedFromFile()) std::cout << "Runtime configuration is valid: " << config.sourcePath() << "\n";
            else std::cout << "No astra.conf found; built-in safe defaults are active.\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "error[E9201]: " << error.what() << "\n";
            return 1;
        }
    }
    if (command == "init") {
        const bool project = argc > 2 && std::string(argv[2]) == "--project";
        if (!project && argc == 2) {
            std::error_code ec;
            const auto cwd = fs::current_path(ec);
            if (!ec && sourceCheckout(cwd)) return installCurrentExecutable(argv[0]);
        }
        const int argIndex = project ? 3 : 2;
        const fs::path target = argc > argIndex ? fs::u8path(argv[argIndex]) : fs::current_path();
        return initProject(target);
    }
    if (command == "check") {
        if (argc < 3) {
            std::cerr << "error[E9003]: Missing source file.\nhelp: astra check <file.astra>\n";
            return 2;
        }
        fs::path source;
        if (!resolveSource(argv[2], source)) {
            std::cerr << "error[E9001]: Source file not found: " << argv[2] << "\n";
            return 2;
        }
        return checkFile(source.string(), true) ? 0 : 1;
    }
    if (command == "file") {
        if (argc < 3) {
            std::cerr << "error[E9003]: Missing source file.\nhelp: astra file <file.astra>\n";
            return 2;
        }
        fs::path source;
        if (!resolveSource(argv[2], source)) {
            std::cerr << "error[E9001]: Source file not found: " << argv[2] << "\n";
            return 2;
        }
        return runFile(source.string(), {false}) ? 0 : 1;
    }
    if (command == "run") {
        const std::string request = argc > 2 ? argv[2] : manifestEntry().empty() ? "main.astra" : manifestEntry();
        fs::path source;
        if (argc > 2) {
            const auto scripts = manifestScripts();
            const auto script = scripts.find(request);
            if (script != scripts.end()) {
                if (!resolveScriptTarget(script->second, source)) {
                    std::cerr << "error[E9010]: Invalid project script `" << request << "`.\nhelp: Script values must point to an Astra source file.\n";
                    return 2;
                }
                return runFile(source.string(), {false}) ? 0 : 1;
            }
        }
        if (!resolveSource(request, source)) {
            std::cerr << "error[E9001]: Source file or project script not found: " << request << "\nhelp: astra run <script|file.astra>\n";
            return 2;
        }
        return runFile(source.string(), {false}) ? 0 : 1;
    }

    fs::path source;
    if (resolveSource(command, source)) return runFile(source.string(), {false}) ? 0 : 1;
    std::cerr << "error[E9004]: Unknown command or source file `" << command << "`.\nhelp: Use `astra --help`, `astra run <script|file.astra>`, or `astra file <file.astra>`.\n";
    return 2;
}
