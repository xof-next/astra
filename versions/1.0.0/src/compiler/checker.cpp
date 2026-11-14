#include "astra/checker.hpp"
#include "astra/diagnostic.hpp"
#include "astra/lexer.hpp"
#include "astra/modules.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
namespace astra {
bool checkFile(const std::string& path,bool verbose) {
    std::ifstream file(path);
    if (!file) {
        report({"E9001","Cannot open source file",path,"",1,1});
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source=buffer.str();
    std::vector<std::string> lines;
    std::istringstream input(source);
    std::string line;
    while(std::getline(input,line)) lines.push_back(line);

    Lexer lexer(source);
    const auto tokens=lexer.scan();
    bool ok=true;
    for(const auto& lexError:lexer.errors()) {
        std::cerr << "error[E1002]: " << lexError << "\n";
        ok=false;
    }
    if(!ok) return false;

    std::unordered_set<std::string> constants;
    std::unordered_set<std::string> shadowedGlobals;
    const std::unordered_map<std::string,std::string> globalModules = {
        {"launch","std:launch"},{"server","std:server"},{"dns","std:dns"},{"tcp","std:tcp"},{"events","std:events"},{"json","std:json"},
        {"math","std:math"},{"crypto","std:crypto"},{"fs","std:fs"},
        {"path","std:path"},{"tls","std:tls"}
    };
    auto error=[&](const Token& token,const std::string& code,const std::string& message) {
        const std::string text=token.line>0&&static_cast<size_t>(token.line)<=lines.size()?lines[static_cast<size_t>(token.line-1)]:"";
        report({code,message,path,text,token.line,token.column});
        ok=false;
    };
    for(size_t i=0;i<tokens.size();++i) {
        const Token& token=tokens[i];
        if(token.kind==TokenKind::Identifier&&token.text=="import"&&i+1<tokens.size()&&tokens[i+1].kind==TokenKind::String) {
            const std::string raw=tokens[i+1].text;
            const std::string module=raw.size()>=2?raw.substr(1,raw.size()-2):raw;
            if(module.rfind("std:",0)==0&&!hasModule(module)) error(tokens[i+1],"E3001","Standard module not found: `"+module+"`");
        }
        if(token.kind==TokenKind::Identifier&&(token.text=="const"||token.text=="let"||token.text=="fn"||token.text=="function")&&i+1<tokens.size()&&tokens[i+1].kind==TokenKind::Identifier) {
            if(token.text=="const") constants.insert(tokens[i+1].text);
            shadowedGlobals.insert(tokens[i+1].text);
        }
        if(token.kind==TokenKind::Identifier&&i+2<tokens.size()&&tokens[i+1].text=="."&&tokens[i+2].kind==TokenKind::Identifier) {
            const std::string alias=token.text;
            const std::string member=tokens[i+2].text;
            bool checkedAlias=false;
            for(size_t j=0;j+3<i;++j) {
                if(tokens[j].text=="import"&&tokens[j+1].kind==TokenKind::String&&tokens[j+2].text=="as"&&tokens[j+3].text==alias) {
                    const std::string raw=tokens[j+1].text;
                    const std::string module=raw.size()>=2?raw.substr(1,raw.size()-2):raw;
                    if(module.rfind("std:",0)==0&&!hasExport(module,member)) error(tokens[i+2],"E3002","Module `"+module+"` does not export member `"+member+"`");
                    checkedAlias=true;
                }
            }
            auto global=globalModules.find(alias);
            if(!checkedAlias&&global!=globalModules.end()&&!shadowedGlobals.count(alias)&&!hasExport(global->second,member))
                error(tokens[i+2],"E3002","Global module `"+alias+"` does not export member `"+member+"`");
        }
        if(token.kind==TokenKind::Identifier&&constants.count(token.text)&&i+1<tokens.size()&&tokens[i+1].text=="="&&!(i>0&&(tokens[i-1].text=="const"||tokens[i-1].text=="let"))) error(token,"E2002","Cannot reassign constant `"+token.text+"`");
    }
    if(verbose&&ok) std::cout << "check: " << path << " OK\n";
    return ok;
}
}
