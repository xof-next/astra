#include "astra/advanced_stdlib.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <cstdint>

namespace astra { namespace {
using Args = std::vector<Value>;
Value undefinedValue() { return Value{}; }
Value native(NativeFunction fn) { return Value(std::move(fn)); }
std::shared_ptr<Array> makeArray() { return std::make_shared<Array>(); }
std::shared_ptr<Object> makeObject() { return std::make_shared<Object>(); }
void need(const Args& args, size_t count, const std::string& name) {
    if (args.size() < count) throw std::runtime_error(name + " expects at least " + std::to_string(count) + " argument(s)");
}
std::shared_ptr<Array> arrayArg(const Args& args, size_t index, const std::string& name) {
    if (index >= args.size()) throw std::runtime_error(name + " is missing an array argument");
    auto value = std::get_if<std::shared_ptr<Array>>(&args[index].data);
    if (!value || !*value) throw std::runtime_error(name + " expects an array");
    return *value;
}
int64_t integerArg(const Args& args, size_t index, const std::string& name) {
    if (index >= args.size() || !std::holds_alternative<int64_t>(args[index].data))
        throw std::runtime_error(name + " expects an integer");
    return std::get<int64_t>(args[index].data);
}
std::string uniqueKey(const Value& value, size_t occurrence) {
    if (std::holds_alternative<Undefined>(value.data)) return "u:";
    if (std::holds_alternative<Null>(value.data)) return "n:";
    if (auto item = std::get_if<bool>(&value.data)) return *item ? "b:1" : "b:0";
    if (auto item = std::get_if<int64_t>(&value.data)) return "i:" + std::to_string(*item);
    if (auto item = std::get_if<double>(&value.data)) {
        if (std::isnan(*item)) return "f:nan:" + std::to_string(occurrence);
        if (*item == 0.0) return "f:0";
        std::ostringstream out; out << std::hexfloat << *item; return "f:" + out.str();
    }
    if (auto item = std::get_if<BigInt>(&value.data)) return "g:" + item->toString();
    if (auto item = std::get_if<std::string>(&value.data)) return "s:" + std::to_string(item->size()) + ":" + *item;
    if (auto item = std::get_if<std::shared_ptr<Array>>(&value.data)) return "a:" + std::to_string(reinterpret_cast<uintptr_t>(item->get()));
    if (auto item = std::get_if<std::shared_ptr<Object>>(&value.data)) return "o:" + std::to_string(reinterpret_cast<uintptr_t>(item->get()));
    if (auto item = std::get_if<std::shared_ptr<Function>>(&value.data)) return "fn:" + std::to_string(reinterpret_cast<uintptr_t>(item->get()));
    // Native std::function has no portable identity operation; retain each occurrence.
    return "native:" + std::to_string(occurrence);
}
std::string groupKey(const Value& value) {
    if (std::holds_alternative<Undefined>(value.data)) return "#undefined";
    if (std::holds_alternative<Null>(value.data)) return "#null";
    if (auto item = std::get_if<bool>(&value.data)) return std::string("#bool:") + (*item ? "true" : "false");
    if (auto item = std::get_if<int64_t>(&value.data)) return "#int:" + std::to_string(*item);
    if (auto item = std::get_if<double>(&value.data)) {
        std::ostringstream out; out << "#float:" << std::hexfloat << *item; return out.str();
    }
    if (auto item = std::get_if<BigInt>(&value.data)) return "#bigint:" + item->toString();
    if (auto item = std::get_if<std::string>(&value.data)) return !item->empty() && item->front() == '#' ? "#string:" + *item : *item;
    throw std::runtime_error("collections.groupBy property must be a primitive value");
}
void flattenInto(const std::shared_ptr<Array>& input, int64_t depth, std::vector<Value>& output) {
    for (const auto& item : input->items) {
        if (depth > 0) {
            if (auto nested = std::get_if<std::shared_ptr<Array>>(&item.data); nested && *nested) {
                flattenInto(*nested, depth - 1, output);
                continue;
            }
        }
        if (output.size() >= 1000000) throw std::runtime_error("collections.flatten exceeds 1000000 output items");
        output.push_back(item);
    }
}
} // namespace

Value createCollectionsBuiltin(const std::string& name) {
    if (name == "std:collections.unique") return native([](const Args& args) {
        need(args, 1, "collections.unique");
        auto input = arrayArg(args, 0, "collections.unique");
        if (input->items.size() > 1000000) throw std::runtime_error("collections.unique input exceeds 1000000 items");
        auto output = makeArray();
        std::unordered_set<std::string> seen;
        seen.reserve(input->items.size());
        for (size_t i = 0; i < input->items.size(); ++i) {
            const std::string key = uniqueKey(input->items[i], i);
            if (seen.insert(key).second) output->items.push_back(input->items[i]);
        }
        return Value(output);
    });
    if (name == "std:collections.chunk") return native([](const Args& args) {
        need(args, 2, "collections.chunk");
        auto input = arrayArg(args, 0, "collections.chunk");
        const int64_t size = integerArg(args, 1, "collections.chunk");
        if (input->items.size() > 1000000) throw std::runtime_error("collections.chunk input exceeds 1000000 items");
        if (size <= 0 || size > 1000000) throw std::runtime_error("collections.chunk size must be between 1 and 1000000");
        auto output = makeArray();
        for (size_t offset = 0; offset < input->items.size(); offset += static_cast<size_t>(size)) {
            auto part = makeArray();
            const size_t end = std::min(input->items.size(), offset + static_cast<size_t>(size));
            part->items.insert(part->items.end(), input->items.begin() + static_cast<std::ptrdiff_t>(offset), input->items.begin() + static_cast<std::ptrdiff_t>(end));
            output->items.emplace_back(part);
        }
        return Value(output);
    });
    if (name == "std:collections.flatten") return native([](const Args& args) {
        need(args, 1, "collections.flatten");
        auto input = arrayArg(args, 0, "collections.flatten");
        const int64_t depth = args.size() > 1 ? integerArg(args, 1, "collections.flatten") : 1;
        if (depth < 0 || depth > 32) throw std::runtime_error("collections.flatten depth must be between 0 and 32");
        if (input->items.size() > 1000000) throw std::runtime_error("collections.flatten input exceeds 1000000 items");
        std::vector<Value> values;
        flattenInto(input, depth, values);
        auto output = makeArray(); output->items = std::move(values);
        return Value(output);
    });
    if (name == "std:collections.zip") return native([](const Args& args) {
        need(args, 2, "collections.zip");
        auto left = arrayArg(args, 0, "collections.zip");
        auto right = arrayArg(args, 1, "collections.zip");
        if (left->items.size() > 1000000 || right->items.size() > 1000000) throw std::runtime_error("collections.zip input exceeds 1000000 items");
        auto output = makeArray();
        const size_t length = std::min(left->items.size(), right->items.size());
        for (size_t i = 0; i < length; ++i) {
            auto pair = makeArray(); pair->items.push_back(left->items[i]); pair->items.push_back(right->items[i]);
            output->items.emplace_back(pair);
        }
        return Value(output);
    });
    if (name == "std:collections.groupBy") return native([](const Args& args) {
        need(args, 2, "collections.groupBy");
        auto input = arrayArg(args, 0, "collections.groupBy");
        if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("collections.groupBy expects a property name string");
        const std::string property = std::get<std::string>(args[1].data);
        if (input->items.size() > 1000000) throw std::runtime_error("collections.groupBy input exceeds 1000000 items");
        auto output = makeObject();
        for (const auto& item : input->items) {
            auto object = std::get_if<std::shared_ptr<Object>>(&item.data);
            if (!object || !*object) throw std::runtime_error("collections.groupBy array values must be objects");
            auto field = (*object)->fields.find(property);
            if (field == (*object)->fields.end()) throw std::runtime_error("collections.groupBy missing property `" + property + "`");
            const std::string key = groupKey(field->second);
            auto found = output->fields.find(key);
            std::shared_ptr<Array> group;
            if (found == output->fields.end()) { group = makeArray(); output->fields.emplace(key, Value(group)); }
            else group = std::get<std::shared_ptr<Array>>(found->second.data);
            group->items.push_back(item);
        }
        return Value(output);
    });
    return undefinedValue();
}
} // namespace astra
