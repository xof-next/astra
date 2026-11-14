#include "astra/interpreter.hpp"
#include "astra/diagnostic.hpp"
#include "astra/lexer.hpp"
#include "astra/modules.hpp"
#include "astra/stdlib.hpp"
#include "astra/value.hpp"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace astra { namespace {
struct RuntimeFailure : std::runtime_error { Token token; std::string code; RuntimeFailure(const std::string&m,const Token&t,const std::string&c="E6001"):std::runtime_error(m),token(t),code(c){} };
struct ReturnSignal { Value value; };
thread_local std::vector<std::shared_ptr<Environment>>* activeEnvironmentRegistry = nullptr;

std::shared_ptr<Environment> makeChildEnvironment(const std::shared_ptr<Environment>& parent) {
    auto child = std::make_shared<Environment>();
    child->parent = parent;
    if (parent) parent->children.emplace_back(child);
    // Keep every scope alive until the execution-wide cycle cleanup runs.
    if (activeEnvironmentRegistry) activeEnvironmentRegistry->push_back(child);
    return child;
}

void clearEnvironmentTree(const std::vector<std::shared_ptr<Environment>>& roots) {
    if (roots.empty()) return;

    std::vector<std::shared_ptr<Environment>> pendingEnvironments(roots.begin(), roots.end());
    std::vector<std::shared_ptr<Environment>> environments;
    std::vector<Value> pendingValues;
    std::vector<std::shared_ptr<Array>> arrays;
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<std::shared_ptr<Function>> functions;
    std::unordered_set<const Environment*> visitedEnvironments;
    std::unordered_set<const Array*> visitedArrays;
    std::unordered_set<const Object*> visitedObjects;
    std::unordered_set<const Function*> visitedFunctions;

    // Walk the full runtime object graph before clearing it. Environments keep
    // child scopes weakly, while closures, arrays and objects may form strong
    // reference cycles that ordinary shared_ptr destruction cannot collect.
    while (!pendingEnvironments.empty() || !pendingValues.empty()) {
        while (!pendingEnvironments.empty()) {
            auto current = std::move(pendingEnvironments.back());
            pendingEnvironments.pop_back();
            if (!current || !visitedEnvironments.insert(current.get()).second) continue;
            for (const auto& weakChild : current->children) {
                if (auto child = weakChild.lock()) pendingEnvironments.emplace_back(std::move(child));
            }
            for (const auto& entry : current->values) pendingValues.push_back(entry.second);
            environments.emplace_back(std::move(current));
        }

        while (!pendingValues.empty()) {
            Value value = std::move(pendingValues.back());
            pendingValues.pop_back();
            if (auto array = std::get_if<std::shared_ptr<Array>>(&value.data)) {
                if (*array && visitedArrays.insert(array->get()).second) {
                    arrays.push_back(*array);
                    for (const auto& item : (*array)->items) pendingValues.push_back(item);
                }
            } else if (auto object = std::get_if<std::shared_ptr<Object>>(&value.data)) {
                if (*object && visitedObjects.insert(object->get()).second) {
                    objects.push_back(*object);
                    for (const auto& field : (*object)->fields) pendingValues.push_back(field.second);
                }
            } else if (auto function = std::get_if<std::shared_ptr<Function>>(&value.data)) {
                if (*function && visitedFunctions.insert(function->get()).second) {
                    functions.push_back(*function);
                    if ((*function)->closure) pendingEnvironments.push_back((*function)->closure);
                }
            }
        }
    }

    // Break collection and closure cycles before releasing environment roots.
    for (auto& array : arrays) array->items.clear();
    for (auto& object : objects) {
        object->fields.clear();
        object->moduleName.clear();
    }
    for (auto& function : functions) {
        function->closure.reset();
        function->invoke = {};
        function->body.clear();
        function->parameters.clear();
    }
    for (auto& environment : environments) {
        environment->values.clear();
        environment->constants.clear();
        environment->children.clear();
        environment->parent.reset();
    }
}

struct EnvironmentCleanup {
    std::vector<std::shared_ptr<Environment>> environments;
    std::vector<std::shared_ptr<Environment>>* previousRegistry = nullptr;

    explicit EnvironmentCleanup(const std::shared_ptr<Environment>& root)
        : previousRegistry(activeEnvironmentRegistry) {
        if (root) environments.push_back(root);
        activeEnvironmentRegistry = &environments;
    }

    ~EnvironmentCleanup() {
        activeEnvironmentRegistry = previousRegistry;
        clearEnvironmentTree(environments);
    }
};
class Parser {
    std::vector<Token> tokens_; size_t pos_=0; std::shared_ptr<Environment> env_; std::string file_; std::vector<std::string> sourceLines_; bool allowAwait_=false;
    const Token& peek(size_t n=0)const{return tokens_[std::min(pos_+n,tokens_.size()-1)];}
    bool at(const std::string&s)const{return peek().text==s;}
    Token take(){return tokens_[pos_++];}
    bool match(const std::string&s){if(at(s)){++pos_;return true;}return false;}
    Token expect(const std::string&s){if(!at(s))fail("E1001","Expected `"+s+"`, received `"+peek().text+"`");return take();}
    [[noreturn]] void fail(const std::string&code,const std::string&msg)const{throw RuntimeFailure(msg,peek(),code);}
    std::string lineText(int n)const{return n>0&&static_cast<size_t>(n)<=sourceLines_.size()?sourceLines_[static_cast<size_t>(n-1)]:"";}
    bool isNumber(const Value&v)const{return std::holds_alternative<int64_t>(v.data)||std::holds_alternative<double>(v.data);}
    double asDouble(const Value&v){if(auto p=std::get_if<int64_t>(&v.data))return static_cast<double>(*p);if(auto p=std::get_if<double>(&v.data))return *p;throw RuntimeFailure("Expected number, received "+valueType(v),peek(),"E4001");}
    int64_t checkedIndex(const Value& value) {
        const double number = asDouble(value);
        const long double wide = static_cast<long double>(number);
        const long double minimum = static_cast<long double>(INT64_MIN);
        const long double maximumExclusive = -minimum;
        if (!std::isfinite(number) || std::floor(number) != number || wide < minimum || wide >= maximumExclusive)
            throw RuntimeFailure("Index must be a finite integer within the signed 64-bit range", peek(), "E6007");
        return static_cast<int64_t>(number);
    }
    int64_t checkedAdd(int64_t a,int64_t b){if((b>0&&a>INT64_MAX-b)||(b<0&&a<INT64_MIN-b))fail("E6002","Integer overflow");return a+b;}
    int64_t checkedSubtract(int64_t a,int64_t b){if((b<0&&a>INT64_MAX+b)||(b>0&&a<INT64_MIN+b))fail("E6002","Integer overflow");return a-b;}
    int64_t checkedMultiply(int64_t a,int64_t b){if(a==0||b==0)return 0;if((a==-1&&b==INT64_MIN)||(b==-1&&a==INT64_MIN))fail("E6002","Integer overflow");if(a>0){if((b>0&&a>INT64_MAX/b)||(b<0&&b<INT64_MIN/a))fail("E6002","Integer overflow");}else{if((b>0&&a<INT64_MIN/b)||(b<0&&a<INT64_MAX/b))fail("E6002","Integer overflow");}return a*b;}
    Value getProperty(const Value&base,const std::string&key,const Token& keyToken){
        if(std::holds_alternative<Undefined>(base.data)||std::holds_alternative<Null>(base.data))throw RuntimeFailure("Cannot read property `"+key+"` of "+valueType(base),keyToken,"E6003");
        if(auto p=std::get_if<std::shared_ptr<Object>>(&base.data)){auto i=(*p)->fields.find(key);if(i==(*p)->fields.end()){if(!(*p)->moduleName.empty())throw RuntimeFailure("Module `"+(*p)->moduleName+"` does not export member `"+key+"`",keyToken,"E3002");return Value{};}return i->second;}
        if(auto p=std::get_if<std::shared_ptr<Array>>(&base.data)){if(key=="length")return Value(static_cast<int64_t>((*p)->items.size()));}
        if(auto p=std::get_if<std::string>(&base.data)){if(key=="length")return Value(static_cast<int64_t>(p->size()));}
        fail("E4002","Type `"+valueType(base)+"` has no property `"+key+"`");
    }
    Value getIndex(const Value&base,const Value&index){if(auto p=std::get_if<std::shared_ptr<Array>>(&base.data)){int64_t i=checkedIndex(index);if(i<0||static_cast<size_t>(i)>=(*p)->items.size())return Value{};return (*p)->items[static_cast<size_t>(i)];}if(auto p=std::get_if<std::string>(&base.data)){int64_t i=checkedIndex(index);if(i<0||static_cast<size_t>(i)>=p->size())return Value{};return Value(std::string(1,(*p)[static_cast<size_t>(i)]));}if(auto p=std::get_if<std::shared_ptr<Object>>(&base.data)){auto i=(*p)->fields.find(valueToString(index));return i==(*p)->fields.end()?Value{}:i->second;}fail("E4002","Value of type "+valueType(base)+" cannot be indexed");}
    Value call(const Value&callee,const std::vector<Value>&args){
        if(auto p=std::get_if<NativeFunction>(&callee.data)){try{return (*p)(args);}catch(const RuntimeFailure&){throw;}catch(const std::exception&e){throw RuntimeFailure(e.what(),peek(),"E6006");}}
        if(auto p=std::get_if<std::shared_ptr<Object>>(&callee.data)){auto i=(*p)->fields.find("__call");if(i!=(*p)->fields.end())return call(i->second,args);}
        if(auto p=std::get_if<std::shared_ptr<Function>>(&callee.data)){auto fn=*p;if(fn->invoke)return fn->invoke(args);if(args.size()!=fn->parameters.size())throw RuntimeFailure("Function expects "+std::to_string(fn->parameters.size())+" argument(s), received "+std::to_string(args.size()),peek(),"E5001");auto local=makeChildEnvironment(fn->closure);for(size_t i=0;i<args.size();++i)local->define(fn->parameters[i],args[i]);try{Parser nested(fn->body,local,file_,sourceLines_,fn->sync);nested.executeAll();}catch(const ReturnSignal&r){return r.value;}return Value{};}
        fail("E5002","Value of type `"+valueType(callee)+"` is not callable");
    }
    bool parenthesizedArrowAhead() const {
        if (!at("(")) return false;
        size_t i=pos_+1;
        if (tokens_[std::min(i,tokens_.size()-1)].text==")") return tokens_[std::min(i+1,tokens_.size()-1)].text=="=>";
        for (;;) {
            const Token& param=tokens_[std::min(i,tokens_.size()-1)];
            if (param.kind!=TokenKind::Identifier) return false;
            ++i;
            const std::string next=tokens_[std::min(i,tokens_.size()-1)].text;
            if (next==")") return tokens_[std::min(i+1,tokens_.size()-1)].text=="=>";
            if (next!=",") return false;
            ++i;
        }
    }
    std::vector<Token> arrowBlockBody(const Token& where) {
        expect("{"); int depth=1; std::vector<Token> body;
        while (peek().kind!=TokenKind::End && depth>0) {
            Token t=take();
            if (t.text=="{") ++depth;
            if (t.text=="}") { --depth; if (depth==0) break; }
            body.push_back(t);
        }
        if (depth!=0) throw RuntimeFailure("Unterminated arrow function body",where,"E1001");
        body.push_back({TokenKind::End,"",peek().line,peek().column});
        return body;
    }
    std::vector<Token> arrowExpressionBody(const Token& where) {
        std::vector<Token> expressionTokens; int parens=0,brackets=0,braces=0;
        while (peek().kind!=TokenKind::End) {
            const std::string x=peek().text;
            if (parens==0 && brackets==0 && braces==0 && (x==";"||x==","||x==")"||x=="]"||x=="}")) break;
            Token t=take();
            if (t.text=="(") ++parens; else if (t.text==")") --parens;
            else if (t.text=="[") ++brackets; else if (t.text=="]") --brackets;
            else if (t.text=="{") ++braces; else if (t.text=="}") --braces;
            expressionTokens.push_back(t);
        }
        if (expressionTokens.empty()) throw RuntimeFailure("Arrow function requires an expression or block body",where,"E1001");
        std::vector<Token> body;
        body.push_back({TokenKind::Identifier,"return",where.line,where.column});
        body.insert(body.end(),expressionTokens.begin(),expressionTokens.end());
        body.push_back({TokenKind::Symbol,";",where.line,where.column});
        body.push_back({TokenKind::End,"",peek().line,peek().column});
        return body;
    }
    Value makeArrowFunction(const std::vector<std::string>& params,std::vector<Token> body,const Token& where) {
        auto fn=std::make_shared<Function>(); fn->parameters=params; fn->body=std::move(body); fn->closure=env_;
        std::weak_ptr<Function> weakFunction=fn; const std::string functionFile=file_; const auto functionLines=sourceLines_; const Token functionToken=where;
        fn->invoke=[weakFunction,functionFile,functionLines,functionToken](const std::vector<Value>& args)->Value {
            auto live=weakFunction.lock();
            if(!live) throw RuntimeFailure("Arrow function is no longer available",functionToken,"E5002");
            if(args.size()!=live->parameters.size()) throw RuntimeFailure("Function expects "+std::to_string(live->parameters.size())+" argument(s), received "+std::to_string(args.size()),functionToken,"E5001");
            auto local=makeChildEnvironment(live->closure);
            for(size_t i=0;i<args.size();++i) local->define(live->parameters[i],args[i]);
            try { Parser nested(live->body,local,functionFile,functionLines); nested.executeAll(); }
            catch(const ReturnSignal& r) { return r.value; }
            return Value{};
        };
        return Value(fn);
    }
    Value parseArrowFunction(bool rustClosure=false) {
        Token where=peek(); std::vector<std::string> params;
        if (rustClosure) {
            if (match("||")) { /* zero-argument Rust-style closure */ }
            else { expect("|"); if(!at("|")){ do { Token p=take(); if(p.kind!=TokenKind::Identifier) throw RuntimeFailure("Expected closure parameter",p,"E5002"); params.push_back(p.text); } while(match(",")&&!at("|")); } expect("|"); }
        } else if (match("(")) {
            if(!at(")")){ do { Token p=take(); if(p.kind!=TokenKind::Identifier) throw RuntimeFailure("Expected arrow parameter",p,"E5002"); params.push_back(p.text); } while(match(",")&&!at(")")); }
            expect(")"); expect("=>");
        } else {
            Token p=take(); if(p.kind!=TokenKind::Identifier) throw RuntimeFailure("Expected arrow parameter",p,"E5002"); params.push_back(p.text); expect("=>");
        }
        std::vector<Token> body=at("{")?arrowBlockBody(where):arrowExpressionBody(where);
        return makeArrowFunction(params,std::move(body),where);
    }
    Value primary(){
        if (match("await")) {
            if (!allowAwait_) fail("E8002", "`await` is only allowed inside a `sync fn` function");
            // Native I/O currently returns completed values. This keeps await
            // syntax explicit without pretending the preview has async tasks.
            return primary();
        }
        if (at("async") && (peek(1).text=="(" || (peek(1).kind==TokenKind::Identifier && peek(2).text=="=>")))
            fail("E8001","async arrow functions are not implemented in this runtime preview");
        if (at("(") && parenthesizedArrowAhead()) return parseArrowFunction(false);
        if (peek().kind==TokenKind::Identifier && peek(1).text=="=>") return parseArrowFunction(false);
        if (at("|") || at("||")) return parseArrowFunction(true);
        if(match("!"))return Value(!isTruthy(primary()));
        if(match("-")){Value v=primary();if(auto p=std::get_if<int64_t>(&v.data)){if(*p==INT64_MIN)fail("E6002","Integer overflow");return Value(-*p);}if(auto p=std::get_if<double>(&v.data))return Value(-*p);if(auto p=std::get_if<BigInt>(&v.data)){BigInt x=*p;if(x.digits!="0")x.negative=!x.negative;return Value(x);}fail("E4001","Unary '-' expects a numeric value");}
        if(match("+")){Value v=primary();if(!isNumber(v))fail("E4001","Unary '+' expects a numeric value");return v;}
        Token t=take();
        if(t.kind==TokenKind::Number){try{std::string n=t.text; n.erase(std::remove(n.begin(),n.end(),'_'),n.end());if(!n.empty()&&n.back()=='n')return Value(BigInt::parse(n.substr(0,n.size()-1)));if(n.find_first_of(".eE")!=std::string::npos)return Value(std::stod(n));return Value(static_cast<int64_t>(std::stoll(n)));}catch(...){throw RuntimeFailure("Invalid or out-of-range numeric literal `"+t.text+"`",t,"E4003");}}
        if(t.kind==TokenKind::String){if(t.text.size()<2)return Value("");char q=t.text.front();std::string s=t.text.substr(1,t.text.size()-2);if(q=='`')return Value(interpolate(s,t));return Value(unescape(s));}
        if(t.text=="true") return Value(true);
        if(t.text=="false") return Value(false);
        if(t.text=="null") return Value(Null{});
        if(t.text=="undefined") return Value{};
        if(t.text=="("){Value v=expression();expect(")");return v;}
        if(t.text=="["){auto a=std::make_shared<Array>();if(!at("]")){do{a->items.push_back(expression());}while(match(",")&&!at("]"));}expect("]");return Value(a);}
        if(t.text=="{"){auto o=std::make_shared<Object>();if(!at("}")){do{Token k=take();if(k.kind!=TokenKind::Identifier&&k.kind!=TokenKind::String)throw RuntimeFailure("Object key must be an identifier or string",k,"E1001");std::string key=k.text;if(k.kind==TokenKind::String)key=unescape(key.substr(1,key.size()-2));expect(":");o->fields[key]=expression();}while(match(",")&&!at("}"));}expect("}");return Value(o);}
        if(t.kind==TokenKind::Identifier){Value v=env_->get(t.text);if(std::holds_alternative<Undefined>(v.data)&&t.text!="undefined")throw RuntimeFailure("Undefined variable `"+t.text+"`",t,"E2001");return v;}
        throw RuntimeFailure("Unexpected token `"+t.text+"`",t,"E1001");
    }
    std::string unescape(const std::string&s){std::string o;for(size_t i=0;i<s.size();++i){if(s[i]=='\\'&&i+1<s.size()){char c=s[++i];switch(c){case 'n':o+='\n';break;case 'r':o+='\r';break;case 't':o+='\t';break;case '0':o+='\0';break;default:o+=c;}}else o+=s[i];}return o;}
    std::string interpolate(const std::string&s,const Token&where){std::string out;for(size_t i=0;i<s.size();){if(s[i]=='$'&&i+1<s.size()&&s[i+1]=='{'){size_t e=s.find('}',i+2);if(e==std::string::npos)throw RuntimeFailure("Unclosed string interpolation",where,"E1002");std::string expr=s.substr(i+2,e-i-2);Lexer lx(expr);Parser p(lx.scan(),env_,file_,sourceLines_,allowAwait_);out+=valueToString(p.expressionOnly());i=e+1;}else out+=s[i++];}return out;}
    Value postfix(){Value v=primary();for(;;){if(match(".")){Token k=take();if(k.kind!=TokenKind::Identifier)fail("E1001","Expected property name after `.`");v=getProperty(v,k.text,k);}else if(match("?.")){Token k=take();if(k.kind!=TokenKind::Identifier)fail("E1001","Expected property name after `?.`");if(std::holds_alternative<Undefined>(v.data)||std::holds_alternative<Null>(v.data))v=Value{};else v=getProperty(v,k.text,k);}else if(match("[")){Value idx=expression();expect("]");v=getIndex(v,idx);}else if(match("(")){std::vector<Value>a;if(!at(")")){do{a.push_back(expression());}while(match(",")&&!at(")"));}expect(")");v=call(v,a);}else break;}return v;}
    int precedence(const std::string&op){if(op=="??")return 1;if(op=="||")return 2;if(op=="&&")return 3;if(op=="=="||op=="!="||op=="==="||op=="!==")return 4;if(op=="<"||op==">"||op=="<="||op==">=")return 5;if(op=="+"||op=="-")return 6;if(op=="*"||op=="/"||op=="%")return 7;if(op=="**")return 8;return 0;}
    Value binary(Value left,const std::string&op,const Value&right){if(op=="??")return (std::holds_alternative<Undefined>(left.data)||std::holds_alternative<Null>(left.data))?right:left;if(op=="||")return isTruthy(left)?left:right;if(op=="&&")return isTruthy(left)?right:left;
        if(op=="=="||op=="==="||op=="!="||op=="!=="){bool eq=false;if(valueType(left)=="bigint"&&valueType(right)=="bigint")eq=BigInt::compare(std::get<BigInt>(left.data),std::get<BigInt>(right.data))==0;if(valueType(left)==valueType(right))eq=valueToString(left)==valueToString(right);else if(isNumber(left)&&isNumber(right))eq=asDouble(left)==asDouble(right);else if(op=="==")eq=valueToString(left)==valueToString(right);return Value((op=="!="||op=="!==")?!eq:eq);}
        if(valueType(left)=="bigint"||valueType(right)=="bigint"){
            if(valueType(left)!="bigint"||valueType(right)!="bigint")throw RuntimeFailure("Cannot mix bigint with other numeric types",peek(),"E4004");
            const auto&a=std::get<BigInt>(left.data);const auto&b=std::get<BigInt>(right.data);
            if(op=="+") return Value(BigInt::add(a,b));
            if(op=="-") return Value(BigInt::subtract(a,b));
            if(op=="*") return Value(BigInt::multiply(a,b));
            if(op=="/") {
                if(b.digits=="0") throw RuntimeFailure("Division by zero",peek(),"E6001");
                return Value(BigInt::divide(a,b));
            }
            if(op=="%") {
                if(b.digits=="0") throw RuntimeFailure("Division by zero",peek(),"E6001");
                return Value(BigInt::remainder(a,b));
            }
            if(op=="<"||op==">"||op=="<="||op==">") {
                int c=BigInt::compare(a,b);
                return Value(op=="<"?c<0:op==">"?c>0:op=="<="?c<=0:c>=0);
            }
            throw RuntimeFailure("Operator `"+op+"` is not supported for bigint",peek(),"E4004");
        }
        if(op=="+"){if(valueType(left)=="string"||valueType(right)=="string")return Value(valueToString(left)+valueToString(right));}
        if(op=="<"||op==">"||op=="<="||op==">="){if(valueType(left)=="string"&&valueType(right)=="string"){auto a=valueToString(left),b=valueToString(right);return Value(op=="<"?a<b:op==">"?a>b:op=="<="?a<=b:a>=b);}double a=asDouble(left),b=asDouble(right);return Value(op=="<"?a<b:op==">"?a>b:op=="<="?a<=b:a>=b);}
        bool ints=valueType(left)=="int"&&valueType(right)=="int";
        if(ints){int64_t a=std::get<int64_t>(left.data),b=std::get<int64_t>(right.data);if((op=="/"||op=="%")&&b==0)throw RuntimeFailure("Division by zero",peek(),"E6001");if(op=="+")return Value(checkedAdd(a,b));if(op=="-")return Value(checkedSubtract(a,b));if(op=="*")return Value(checkedMultiply(a,b));if(op=="/"){if(a==INT64_MIN&&b==-1)fail("E6002","Integer overflow");return Value(a/b);}if(op=="%"){if(a==INT64_MIN&&b==-1)return Value(int64_t(0));return Value(a%b);}if(op=="<")return Value(a<b);if(op==">")return Value(a>b);if(op=="<=")return Value(a<=b);if(op==">=")return Value(a>=b);}
        double a=asDouble(left),b=asDouble(right);if(op=="+")return Value(a+b);if(op=="-")return Value(a-b);if(op=="*")return Value(a*b);if(op=="/")return Value(a/b);if(op=="%")return Value(std::fmod(a,b));if(op=="**")return Value(std::pow(a,b));return Value{};}
    Value expression(int minPrec=1){Value left=postfix();while(true){std::string op=peek().text;int p=precedence(op);if(p<minPrec||p==0)break;take();Value right=expression(p+(op=="**"?0:1));left=binary(left,op,right);}return left;}
    Value expressionOnly(){Value v=expression();return v;}
    void importStatement(){Token path=take();if(path.kind!=TokenKind::String)throw RuntimeFailure("Import path must be a string",path,"E3001");std::string mod=path.text.substr(1,path.text.size()-2);expect("as");Token alias=take();if(alias.kind!=TokenKind::Identifier)throw RuntimeFailure("Expected module alias",alias,"E3001");if(mod.rfind("std:",0)==0){try{env_->define(alias.text,createBuiltinModule(mod,{}));}catch(const std::exception&e){throw RuntimeFailure(e.what(),path,"E3001");}}else throw RuntimeFailure("External and local module loading is not implemented in this build: `"+mod+"`",path,"E3003");match(";");}
    void variableStatement(bool constant){Token name=take();if(name.kind!=TokenKind::Identifier)throw RuntimeFailure("Expected variable name",name,"E2001");Value value;if(match("="))value=expression();if(!env_->define(name.text,value,constant))throw RuntimeFailure("Variable `"+name.text+"` is already declared in this scope",name,"E2003");match(";");}
    void functionStatement(bool syncFunction=false){Token name=take();if(name.kind!=TokenKind::Identifier)throw RuntimeFailure("Expected function name",name,"E5002");expect("(");std::vector<std::string>params;if(!at(")")){do{Token p=take();if(p.kind!=TokenKind::Identifier)throw RuntimeFailure("Expected parameter name",p,"E5002");params.push_back(p.text);}while(match(",")&&!at(")"));}expect(")");expect("{");int depth=1;std::vector<Token>body;while(!at("")&&depth>0){Token t=take();if(t.text=="{")++depth;if(t.text=="}"){--depth;if(depth==0)break;}body.push_back(t);}if(depth!=0)throw RuntimeFailure("Unterminated function body",name,"E1001");body.push_back({TokenKind::End,"",name.line,name.column});auto f=std::make_shared<Function>();f->parameters=params;f->body=body;f->closure=env_;f->sync=syncFunction;std::weak_ptr<Function> weakFunction=f;const std::string functionFile=file_;const auto functionLines=sourceLines_;const Token functionToken=name;f->invoke=[weakFunction,functionFile,functionLines,functionToken](const std::vector<Value>&args)->Value{auto fn=weakFunction.lock();if(!fn)throw RuntimeFailure("Function is no longer available",functionToken,"E5002");if(args.size()!=fn->parameters.size())throw RuntimeFailure("Function expects "+std::to_string(fn->parameters.size())+" argument(s), received "+std::to_string(args.size()),functionToken,"E5001");auto local=makeChildEnvironment(fn->closure);for(size_t i=0;i<args.size();++i)local->define(fn->parameters[i],args[i]);try{Parser nested(fn->body,local,functionFile,functionLines,fn->sync);nested.executeAll();}catch(const ReturnSignal&r){return r.value;}return Value{};};if(!env_->define(name.text,Value(f)))throw RuntimeFailure("Function `"+name.text+"` is already declared",name,"E2003");}
    void block(){expect("{");auto previous=env_;env_=makeChildEnvironment(previous);while(!at("}")&&peek().kind!=TokenKind::End)statement();expect("}");env_=previous;}
    void ifStatement(){expect("(");Value cond=expression();expect(")");if(isTruthy(cond))statement();else{skipStatement();if(match("else"))statement();return;}if(match("else"))skipStatement();}
    void skipStatement(){if(match("{")){int d=1;while(d>0&&peek().kind!=TokenKind::End){if(match("{"))++d;else if(match("}"))--d;else take();}}else{while(peek().kind!=TokenKind::End&&!match(";"))take();}}
    void whileStatement(){expect("(");size_t condStart=pos_;Value cond=expression();expect(")");std::vector<Token>body=statementTokens();int guard=0;while(isTruthy(cond)){if(++guard>1000000)fail("E6004","Loop iteration limit exceeded");Parser p(body,env_,file_,sourceLines_,allowAwait_);p.executeAll();Parser c(tokens_,env_,file_,sourceLines_,allowAwait_);c.pos_=condStart;cond=c.expression();}}
    std::vector<Token> statementTokens(){if(match("{")){int d=1;std::vector<Token>b;while(d>0&&peek().kind!=TokenKind::End){Token t=take();if(t.text=="{")++d;if(t.text=="}"){--d;if(d==0)break;}b.push_back(t);}b.push_back({TokenKind::End,"",peek().line,peek().column});return b;}std::vector<Token>b;while(peek().kind!=TokenKind::End){Token t=take();b.push_back(t);if(t.text==";")break;}b.push_back({TokenKind::End,"",peek().line,peek().column});return b;}
    void forStatement(){expect("(");if(match("let")||match("const")){bool c=tokens_[pos_-1].text=="const";Token n=take();if(match("of")){Value list=expression();expect(")");auto arr=std::get_if<std::shared_ptr<Array>>(&list.data);if(!arr)fail("E4001","for...of expects an array");auto body=statementTokens();for(const auto&v:(*arr)->items){auto loop=makeChildEnvironment(env_);loop->define(n.text,v,c);Parser p(body,loop,file_,sourceLines_,allowAwait_);p.executeAll();}return;}
            expect("=");Value initial=expression();if(!env_->define(n.text,initial,c))throw RuntimeFailure("Variable already declared `"+n.text+"`",n,"E2003");expect(";");
        }else if(!at(";")){Value ignored=expression();(void)ignored;expect(";");}else take();
        std::vector<Token> condTokens;while(!at(";")&&peek().kind!=TokenKind::End)condTokens.push_back(take());expect(";");std::vector<Token> stepTokens;while(!at(")")&&peek().kind!=TokenKind::End)stepTokens.push_back(take());expect(")");condTokens.push_back({TokenKind::End,"",peek().line,peek().column});stepTokens.push_back({TokenKind::End,"",peek().line,peek().column});auto body=statementTokens();int guard=0;
        auto condition=[&](){if(condTokens.size()==1)return true;Parser p(condTokens,env_,file_,sourceLines_,allowAwait_);return isTruthy(p.expressionOnly());};
        while(condition()){if(++guard>1000000)fail("E6004","Loop iteration limit exceeded");Parser p(body,env_,file_,sourceLines_,allowAwait_);p.executeAll();Parser step(stepTokens,env_,file_,sourceLines_,allowAwait_);step.executeAll();}
    }
    void tryStatement(){auto body=statementTokens();try{Parser p(body,env_,file_,sourceLines_,allowAwait_);p.executeAll();}catch(const RuntimeFailure&e){if(!match("catch"))throw;expect("(");Token name=take();expect(")");auto local=makeChildEnvironment(env_);auto err=std::make_shared<Object>();err->fields["message"]=Value(std::string(e.what()));err->fields["name"]=Value(std::string("AstraError"));local->define(name.text,Value(err));Parser p(statementTokens(),local,file_,sourceLines_,allowAwait_);p.executeAll();return;}if(match("catch")){expect("(");take();expect(")");skipStatement();}}
public:
    Parser(std::vector<Token> t,std::shared_ptr<Environment>e,std::string f,std::vector<std::string>l,bool allowAwait=false):tokens_(std::move(t)),env_(std::move(e)),file_(std::move(f)),sourceLines_(std::move(l)),allowAwait_(allowAwait){if(tokens_.empty()||tokens_.back().kind!=TokenKind::End)tokens_.push_back({TokenKind::End,"",1,1});}
    void executeAll(){while(peek().kind!=TokenKind::End)statement();}
    Value callMain(){Value m=env_->get("main");if(std::holds_alternative<Undefined>(m.data))return Value{};return call(m,{});}
    void statement(){if(match(";"))return;if(match("import")){importStatement();return;}if(match("pub")){if(match("async"))fail("E8001","`async` functions are not supported; use `pub sync fn` for the current await-capable function syntax");bool syncFunction=match("sync");if(match("fn")||match("function")){functionStatement(syncFunction);return;}fail("E1001","Expected `fn` or `function` after `pub` or `pub sync`");}if(match("async"))fail("E8001","`async` functions are not supported; use `sync fn` for the current await-capable function syntax");if(match("sync")){if(match("fn")||match("function")){functionStatement(true);return;}fail("E1001","Expected `fn` after `sync`");}if(match("fn")||match("function")){functionStatement(false);return;}if(match("let")){variableStatement(false);return;}if(match("const")){variableStatement(true);return;}if(match("if")){ifStatement();return;}if(match("while")){whileStatement();return;}if(match("for")){forStatement();return;}if(match("try")){tryStatement();return;}if(match("throw")){Value v=expression();throw RuntimeFailure(valueToString(v),peek(),"E6005");}if(match("return")){Value v;if(!at(";")&&!at("}")&&peek().kind!=TokenKind::End)v=expression();match(";");throw ReturnSignal{v};}if(match("delete")){Token n=take();if(n.kind!=TokenKind::Identifier)throw RuntimeFailure("delete expects an object property",n,"E1001");Value base=env_->get(n.text);if(match(".")){Token key=take();auto obj=std::get_if<std::shared_ptr<Object>>(&base.data);if(!obj)throw RuntimeFailure("delete target is not an object",n,"E4002");if(!(*obj)->moduleName.empty())throw RuntimeFailure("Module `"+(*obj)->moduleName+"` is read-only",key,"E2002");(*obj)->fields.erase(key.text);}else throw RuntimeFailure("delete currently requires object.property syntax",n,"E1001");match(";");return;}if(at("{")){block();return;}
        if(peek().kind==TokenKind::Identifier&&peek(1).text=="["){
            size_t savedPos=pos_;Token n=take();take();Value index=expression();expect("]");std::string op=peek().text;
            if(op=="="||op=="+="||op=="-="){take();Value base=env_->get(n.text);if(auto arr=std::get_if<std::shared_ptr<Array>>(&base.data)){int64_t at=checkedIndex(index);if(at<0||static_cast<size_t>(at)>=(*arr)->items.size())throw RuntimeFailure("Array index is out of range",n,"E6007");Value value=expression();if(op!="=")value=binary((*arr)->items[static_cast<size_t>(at)],op.substr(0,1),value);(*arr)->items[static_cast<size_t>(at)]=value;}else if(auto obj=std::get_if<std::shared_ptr<Object>>(&base.data)){if(!(*obj)->moduleName.empty())throw RuntimeFailure("Module `"+(*obj)->moduleName+"` is read-only; declare a local binding to shadow it",n,"E2002");std::string key=valueToString(index);Value value=expression();if(op!="="){auto it=(*obj)->fields.find(key);if(it==(*obj)->fields.end())throw RuntimeFailure("Undefined object property `"+key+"`",n,"E4002");value=binary(it->second,op.substr(0,1),value);}(*obj)->fields[key]=value;}else throw RuntimeFailure("Indexed assignment requires an array or object",n,"E4002");match(";");return;}
            pos_=savedPos;
        }
        if(peek().kind==TokenKind::Identifier&&peek(1).text=="."&&(peek(3).text=="="||peek(3).text=="+="||peek(3).text=="-=")){Token n=take();take();Token key=take();std::string op=take().text;Value base=env_->get(n.text);auto obj=std::get_if<std::shared_ptr<Object>>(&base.data);if(!obj)throw RuntimeFailure("Cannot assign property on non-object value",n,"E4002");if(!(*obj)->moduleName.empty())throw RuntimeFailure("Module `"+(*obj)->moduleName+"` is read-only; declare a local binding to shadow it",key,"E2002");Value v=expression();if(op!="="){auto it=(*obj)->fields.find(key.text);if(it==(*obj)->fields.end())throw RuntimeFailure("Undefined property `"+key.text+"`",key,"E4002");v=binary(it->second,op.substr(0,1),v);}(*obj)->fields[key.text]=v;match(";");return;}
        if(peek().kind==TokenKind::Identifier&&(peek(1).text=="="||peek(1).text=="+="||peek(1).text=="-="||peek(1).text=="*="||peek(1).text=="/="||peek(1).text=="++"||peek(1).text=="--")){Token n=take();std::string op=take().text;Value old=env_->get(n.text);if(std::holds_alternative<Undefined>(old.data))throw RuntimeFailure("Undefined variable `"+n.text+"`",n,"E2001");if(env_->isConstant(n.text))throw RuntimeFailure("Cannot reassign constant `"+n.text+"`",n,"E2002");Value v;if(op=="++"||op=="--")v=binary(old,op=="++"?"+":"-",Value(int64_t(1)));else{Value rhs=expression();v=op=="="?rhs:binary(old,op.substr(0,1),rhs);}if(!env_->assign(n.text,v))throw RuntimeFailure("Cannot assign to constant `"+n.text+"`",n,"E2002");match(";");return;}expression();match(";");}
};
std::shared_ptr<Object> callableObject(NativeFunction f){auto o=std::make_shared<Object>();o->fields["__call"]=Value(std::move(f));return o;}
std::shared_ptr<Environment> createGlobalEnvironment(){auto e=std::make_shared<Environment>();auto add=[&](const std::string&n){Value v=createGlobal(n,{});if(!std::holds_alternative<Undefined>(v.data))e->define(n,v,true);};for(auto n:{"console","print","Number","String","str","int","float","bool","bigint","BigInt","type","len","range","input","isUndefined","assert","Error","Array","Object"})add(n);
 // Number is both callable and a namespace of constants.
 auto numberObj=std::get<std::shared_ptr<Object>>(e->get("Number").data);numberObj->fields["__call"]=NativeFunction([](const std::vector<Value>&a){if(a.empty())throw std::runtime_error("Number expects a value");std::string s=valueToString(a[0]);try{size_t n=0;double d=std::stod(s,&n);if(n!=s.size())throw std::runtime_error("Invalid Number input");const long double wide=static_cast<long double>(d);const long double minimum=static_cast<long double>(INT64_MIN);const long double maximumExclusive=-minimum;if(std::isfinite(d)&&std::floor(d)==d&&wide>=minimum&&wide<maximumExclusive)return Value(static_cast<int64_t>(d));return Value(d);}catch(const std::exception&e){throw std::runtime_error(std::string("Cannot convert to Number: ")+e.what());}});e->values["Number"]=Value(numberObj);
 // print is callable and supports print.json(value).
 auto printObj=callableObject(std::get<NativeFunction>(createGlobal("print",{}).data));printObj->fields["json"]=NativeFunction([](const std::vector<Value>&a){if(a.empty())throw std::runtime_error("print.json expects a value");std::cout<<valueToJson(a[0],true)<<'\n';return Value(Null{});});e->values["print"]=Value(printObj);
 // Core standard namespaces are available globally, but live in a parent scope.
 // A project may shadow any of them with fn/let/const without mutating the builtin.
 for (const auto& name : {"launch","server","dns","tcp","events","json","math","crypto","fs","path","tls"}) {
     e->define(name,createBuiltinModule(std::string("std:")+name,{}),true);
 }
 return e;}
}
bool runFile(const std::string&path,const RunOptions&opts){std::ifstream f(path);if(!f){report({"E9001","Cannot open source file",path,"",1,1});return false;}std::ostringstream ss;ss<<f.rdbuf();std::string src=ss.str();std::vector<std::string>lines;std::istringstream ls(src);std::string line;while(std::getline(ls,line))lines.push_back(line);Lexer lx(src);auto tokens=lx.scan();if(!lx.errors().empty()){for(const auto&e:lx.errors())std::cerr<<"error[E1002]: "<<e<<'\n';return false;}auto globals=createGlobalEnvironment();EnvironmentCleanup cleanup{globals};auto env=makeChildEnvironment(globals);try{Parser p(tokens,env,path,lines);p.executeAll();Value main=env->get("main");if(!std::holds_alternative<Undefined>(main.data)){if(auto fn=std::get_if<std::shared_ptr<Function>>(&main.data)){if(!(*fn)->parameters.empty()){std::cerr<<"error[E5001]: main() must not require arguments\n";return false;}}// main is invoked in its captured project environment.
            auto fn=std::get<std::shared_ptr<Function>>(main.data);auto local=makeChildEnvironment(fn->closure);try{Parser body(fn->body,local,path,lines,fn->sync);body.executeAll();}catch(const ReturnSignal&){} }
        if(opts.verbose) std::cout<<"Astra: execution completed\n";
        return true;
    }catch(const RuntimeFailure&e){std::string srcLine=e.token.line>0&&static_cast<size_t>(e.token.line)<=lines.size()?lines[e.token.line-1]:"";report({e.code,e.what(),path,srcLine,e.token.line,e.token.column});return false;}catch(const std::exception&e){report({"E6009",e.what(),path,"",1,1});return false;}catch(const ReturnSignal&){report({"E5003","return used outside a function",path,"",1,1});return false;}}
}
