#include "astra/checker.hpp"
#include "astra/interpreter.hpp"
#include "astra/modules.hpp"
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
std::map<std::string, std::string> manifestTasks() {
    std::map<std::string, std::string> tasks; std::ifstream f("Astra.toml"); if (!f) return tasks;
    std::string line; bool inTasks = false;
    while (std::getline(f, line)) {
        line = trim(line); if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { inTasks = line == "[devDependencies]"; continue; }
        if (!inTasks) continue;
        const auto equal = line.find('='); if (equal == std::string::npos) continue;
        const std::string name = trim(line.substr(0, equal)), command = unquote(line.substr(equal + 1));
        if (!name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; }) && !command.empty()) tasks[name] = command;
    }
    return tasks;
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
    if (fs::exists(directory / "Astra.toml") || fs::exists(directory / "main.astra")) { std::cerr << "error[E9005]: Astra project files already exist in " << directory.string() << "\n"; return 1; }
    std::ofstream manifest(directory / "Astra.toml", std::ios::binary);
    if (!manifest) { std::cerr << "error[E9002]: Cannot create Astra.toml\n"; return 1; }
    manifest << "[project]\nname = \"my-project\"\nversion = \"1.0.0\"\nentry = \"main.astra\"\n\n[devDependencies]\n# Run with: astra test\ntest = \"astra lib/test.astra\"\n";
    manifest.close(); fs::create_directories(directory / "lib", ec);
    if (ec) { std::cerr << "error[E9002]: Cannot create lib directory: " << ec.message() << "\n"; return 1; }
    std::ofstream mainFile(directory / "main.astra", std::ios::binary), testFile(directory / "lib/test.astra", std::ios::binary), namedTestFile(directory / "test.astra", std::ios::binary);
    if (!mainFile || !testFile || !namedTestFile) { std::cerr << "error[E9002]: Failed to write project source files\n"; return 1; }
    mainFile << "pub fn main() {\n    console.log(\"Hello from Astra!\");\n}\n";
    testFile << "pub fn main() {\n    assert(1 + 1 == 2, \"basic arithmetic test failed\");\n    console.log(\"Tests passed\");\n}\n";
    namedTestFile << "pub fn main() {\n    assert(2 * 3 == 6, \"named source test failed\");\n    console.log(\"test.astra executed\");\n}\n";
    std::cout << "Created Astra project at " << directory.string() << "\nFiles: Astra.toml, main.astra, test.astra, lib/test.astra\nRun: astra\nTask: astra test\nFile: astra test.astra\n"; return 0;
}
bool parseTaskCommand(const std::string& command, fs::path& source) {
    // Never invoke a shell: project task aliases can only dispatch Astra files.
    std::istringstream input(command); std::vector<std::string> parts;
    for (std::string part; input >> part;) parts.push_back(part);
    if (parts.size() == 2 && parts[0] == "astra") return resolveSource(parts[1], source);
    if (parts.size() == 3 && parts[0] == "astra" && parts[1] == "run") return resolveSource(parts[2], source);
    return false;
}
void help() {
    std::cout << "Astra Script v" << astra::currentVersion() << "\n\nUsage:\n"
              << "  astra                         Run project entry (Astra.toml or main.astra)\n"
              << "  astra <name|file.astra>       Run a task alias or source file\n"
              << "  astra run [name|file]         Run a source file (extension optional)\n"
              << "  astra check <file>            Run static source checks\n"
              << "  astra modules                 List built-in standard modules\n"
              << "  astra init                    Install globally in an Astra source checkout\n"
              << "  astra init --project [dir]    Create a project in the current/target directory\n"
              << "  astra install [version]       Install a version from pack.json (default: new)\n"
              << "  astra update                  Install and activate pack.json's new version\n"
              << "  astra versions                List published versions from pack.json\n"
              << "  astra use <version>           Activate an already installed version\n"
              << "  astra version | --version     Show runtime version\n"
              << "  astra --help                  Show this help\n\nTask aliases are declared in [devDependencies] in Astra.toml.\n";
}
}
int main(int argc, char** argv) {
    using namespace astra;
    if (argc < 2) {
        std::string entry = manifestEntry(); if (entry.empty()) entry = "main.astra";
        fs::path source;
        if (!resolveSource(entry, source)) { std::cerr << "error[E9003]: No project entry found.\nhelp: Run `astra init --project` or specify `astra <file.astra>`.\n"; return 2; }
        return runFile(source.string(), {false}) ? 0 : 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "-v") { std::cout << "Astra Script " << currentVersion() << " (C++17 interpreter preview)\n"; return 0; }
    if (cmd == "version") { std::cout << "Astra Script " << currentVersion() << "\nRuntime: native C++17 interpreter\nGlobal install path: " << installedRuntimePath() << "\n"; return 0; }
    if (cmd == "install") return installRuntimeVersion(argc > 2 ? argv[2] : "", false);
    if (cmd == "update") return installRuntimeVersion("", true);
    if (cmd == "versions") return listAvailableVersions();
    if (cmd == "use") {
        if (argc < 3) { std::cerr << "error[E9023]: Missing version.\nhelp: astra use <version>\n"; return 2; }
        return useInstalledVersion(argv[2]);
    }
    if (cmd == "--help" || cmd == "help") { help(); return 0; }
    if (cmd == "modules") {
        size_t count = 0; for (const auto& module : builtinModules()) { count += module.exports.size(); std::cout << module.name << " (" << module.exports.size() << ")\n  "; for (size_t i = 0; i < module.exports.size(); ++i) std::cout << (i ? ", " : "") << module.exports[i]; std::cout << "\n"; }
        std::cout << "Total: " << builtinModules().size() << " standard modules, " << count << " exports\n"; return 0;
    }
    if (cmd == "init") {
        const bool project = argc > 2 && std::string(argv[2]) == "--project";
        if (!project && argc == 2) { std::error_code ec; const auto cwd = fs::current_path(ec); if (!ec && sourceCheckout(cwd)) return installCurrentExecutable(argv[0]); }
        const int argIndex = project ? 3 : 2; const fs::path target = argc > argIndex ? fs::u8path(argv[argIndex]) : fs::current_path(); return initProject(target);
    }
    if (cmd == "check") {
        if (argc < 3) { std::cerr << "error[E9003]: Missing source file.\nhelp: astra check <file.astra>\n"; return 2; }
        fs::path source; if (!resolveSource(argv[2], source)) { std::cerr << "error[E9001]: Source file not found: " << argv[2] << "\n"; return 2; }
        return checkFile(source.string(), true) ? 0 : 1;
    }
    if (cmd == "run") {
        std::string request = argc > 2 ? argv[2] : manifestEntry(); if (request.empty()) request = "main.astra";
        fs::path source; if (!resolveSource(request, source)) { std::cerr << "error[E9001]: Source file not found: " << request << "\nhelp: `astra run <name>` also checks for `<name>.astra`.\n"; return 2; }
        return runFile(source.string(), {false}) ? 0 : 1;
    }
    const auto tasks = manifestTasks(); auto task = tasks.find(cmd); fs::path source;
    if (task != tasks.end()) {
        if (!parseTaskCommand(task->second, source)) { std::cerr << "error[E9010]: Invalid Astra task `" << cmd << "`.\nhelp: Use `astra <file.astra>` or `astra run <file.astra>`; shell commands are not executed.\n"; return 2; }
        return runFile(source.string(), {false}) ? 0 : 1;
    }
    if (resolveSource(cmd, source)) return runFile(source.string(), {false}) ? 0 : 1;
    std::cerr << "error[E9004]: Unknown command or source file `" << cmd << "`.\nhelp: Use `astra --help`, declare a task in [devDependencies], or run `astra <file.astra>`.\n"; return 2;
}
