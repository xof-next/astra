#include "astra/lexer.hpp"
#include <cctype>
namespace astra {
std::vector<Token> Lexer::scan() {
    errors_.clear(); std::vector<Token> out; size_t i=0; int line=1,col=1;
    auto push=[&](TokenKind k,size_t s,int l,int c){out.push_back({k,source_.substr(s,i-s),l,c});};
    const std::vector<std::string> multi={"===","!==","=>","==","!=","<=",">=","&&","||","??","?.","++","--","**","+=","-=","*=","/=","%=","..."};
    while(i<source_.size()) {
        char c=source_[i];
        if(c==' '||c=='\t'||c=='\r'){++i;++col;continue;}
        if(c=='\n'){++i;++line;col=1;continue;}
        if(c=='#'){while(i<source_.size()&&source_[i]!='\n'){++i;++col;}continue;}
        if(c=='/'&&i+1<source_.size()&&source_[i+1]=='/'){while(i<source_.size()&&source_[i]!='\n'){++i;++col;}continue;}
        if(c=='/'&&i+1<source_.size()&&source_[i+1]=='*'){
            int startLine=line,startCol=col; i+=2;col+=2; bool closed=false;
            while(i<source_.size()){if(source_[i]=='*'&&i+1<source_.size()&&source_[i+1]=='/'){i+=2;col+=2;closed=true;break;}if(source_[i]=='\n'){++i;++line;col=1;}else{++i;++col;}}
            if(!closed) errors_.push_back("Unterminated block comment at "+std::to_string(startLine)+":"+std::to_string(startCol));
            continue;
        }
        int l=line,cc=col; size_t s=i;
        if(std::isalpha(static_cast<unsigned char>(c))||c=='_'||c=='$'){
            ++i;++col;while(i<source_.size()&&(std::isalnum(static_cast<unsigned char>(source_[i]))||source_[i]=='_'||source_[i]=='$')){++i;++col;}push(TokenKind::Identifier,s,l,cc);continue;
        }
        if(std::isdigit(static_cast<unsigned char>(c))){
            ++i;++col;
            while(i<source_.size()&&(std::isdigit(static_cast<unsigned char>(source_[i]))||source_[i]=='_')){++i;++col;}
            if(i<source_.size()&&source_[i]=='.'&&i+1<source_.size()&&std::isdigit(static_cast<unsigned char>(source_[i+1]))){++i;++col;while(i<source_.size()&&(std::isdigit(static_cast<unsigned char>(source_[i]))||source_[i]=='_')){++i;++col;}}
            if(i<source_.size()&&(source_[i]=='e'||source_[i]=='E')){++i;++col;if(i<source_.size()&&(source_[i]=='+'||source_[i]=='-')){++i;++col;}while(i<source_.size()&&(std::isdigit(static_cast<unsigned char>(source_[i]))||source_[i]=='_')){++i;++col;}}
            if(i<source_.size()&&source_[i]=='n'){++i;++col;}
            push(TokenKind::Number,s,l,cc);continue;
        }
        if(c=='"'||c=='\''||c=='`'){
            char q=c;++i;++col;bool closed=false;
            while(i<source_.size()){
                if(source_[i]=='\\'&&i+1<source_.size()){i+=2;col+=2;continue;}
                if(source_[i]==q){++i;++col;closed=true;break;}
                if(source_[i]=='\n'){++i;++line;col=1;}else{++i;++col;}
            }
            if(!closed)errors_.push_back("Unterminated string at "+std::to_string(l)+":"+std::to_string(cc));
            push(TokenKind::String,s,l,cc);continue;
        }
        bool found=false;
        for(const auto& op:multi){if(source_.compare(i,op.size(),op)==0){i+=op.size();col+=static_cast<int>(op.size());push(TokenKind::Symbol,s,l,cc);found=true;break;}}
        if(found)continue;
        ++i;++col;push(TokenKind::Symbol,s,l,cc);
    }
    out.push_back({TokenKind::End,"",line,col}); return out;
}
}
