#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>
#include "astra/lexer.hpp"
#include "astra/bigint.hpp"

namespace astra {
struct Undefined {};
struct Null {};
struct Array;
struct Object;
struct Function;
struct Future;
struct Value;
using NativeFunction = std::function<Value(const std::vector<Value>&)>;
struct Value {
    using Data = std::variant<Undefined, Null, bool, int64_t, double, BigInt, std::string,
                              std::shared_ptr<Array>, std::shared_ptr<Object>,
                              std::shared_ptr<Function>, std::shared_ptr<Future>, NativeFunction>;
    Data data;
    Value() : data(Undefined{}) {}
    template<class T> Value(T v) : data(std::move(v)) {}
};
struct Array { std::vector<Value> items; };
struct Object { std::map<std::string, Value> fields; std::string moduleName; };
struct Function {
    std::vector<std::string> parameters;
    std::vector<Token> body;
    std::shared_ptr<struct Environment> closure;
    std::function<Value(const std::vector<Value>&)> invoke;
    bool allowAwait = false;
    bool asynchronous = false;
};
struct Future {
    std::shared_future<Value> result;
    explicit Future(std::shared_future<Value> value) : result(std::move(value)) {}
};
struct Environment : std::enable_shared_from_this<Environment> {
    std::map<std::string, Value> values;
    std::set<std::string> constants;
    std::shared_ptr<Environment> parent;
    std::vector<std::shared_ptr<Environment>> children;
    mutable std::mutex mutex;
    bool define(const std::string&, const Value&, bool constant=false);
    Value get(const std::string&) const;
    bool assign(const std::string&, const Value&);
    bool isConstant(const std::string&) const;
};
std::string valueType(const Value&);
std::string valueToString(const Value&);
std::string valueToJson(const Value&, bool pretty=false, int depth=0);
bool isTruthy(const Value&);
Value makeFuture(std::function<Value()> task);
Value resolveAwait(const Value& value);
void waitForAsyncTasks();
}
