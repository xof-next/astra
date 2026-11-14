#include "astra/advanced_stdlib.hpp"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace astra { namespace {
using Args = std::vector<Value>;
struct Listener {
    uint64_t id = 0;
    Value callback;
    bool once = false;
};
struct EventState {
    std::mutex mutex;
    std::map<std::string, std::vector<Listener>> listeners;
    uint64_t nextId = 1;
};
Value undefinedValue() { return Value{}; }
Value native(NativeFunction f) { return Value(std::move(f)); }
std::shared_ptr<Object> makeObject() { return std::make_shared<Object>(); }
std::shared_ptr<Array> makeArray() { return std::make_shared<Array>(); }
void need(const Args& args, size_t n, const std::string& fn) {
    if (args.size() < n) throw std::runtime_error(fn + " expects at least " + std::to_string(n) + " argument(s)");
}
std::string stringArg(const Args& args, size_t i, const std::string& fn) {
    if (i >= args.size() || !std::holds_alternative<std::string>(args[i].data))
        throw std::runtime_error(fn + " expects a string event name");
    return std::get<std::string>(args[i].data);
}
void invokeListener(const Value& callback, const Args& args) {
    if (auto fn = std::get_if<NativeFunction>(&callback.data)) {
        (*fn)(args);
        return;
    }
    if (auto fn = std::get_if<std::shared_ptr<Function>>(&callback.data)) {
        if (!*fn || !(*fn)->invoke) throw std::runtime_error("Event callback is not executable");
        (*fn)->invoke(args);
        return;
    }
    if (auto obj = std::get_if<std::shared_ptr<Object>>(&callback.data)) {
        auto it = (*obj)->fields.find("__call");
        if (it != (*obj)->fields.end()) {
            if (auto fn = std::get_if<NativeFunction>(&it->second.data)) { (*fn)(args); return; }
        }
    }
    throw std::runtime_error("events.on expects a function callback");
}
} // namespace

Value createAdvancedModule(const std::string& moduleName) {
    if (moduleName == "std:server") return createServerModule(moduleName);
    if (moduleName == "std:dns" || moduleName == "std:tcp") return createNetworkModule(moduleName);
    if (moduleName != "std:events") return undefinedValue();
    // Standard modules are process singletons: multiple imports share one event bus.
    static std::shared_ptr<EventState> sharedState = std::make_shared<EventState>();
    auto state = sharedState;
    auto out = makeObject();
    out->moduleName = moduleName;

    out->fields["on"] = native([state](const Args& args) {
        need(args, 2, "events.on");
        const std::string name = stringArg(args, 0, "events.on");
        const Value callback = args[1];
        const bool valid = std::holds_alternative<NativeFunction>(callback.data) ||
            (std::holds_alternative<std::shared_ptr<Function>>(callback.data) &&
             static_cast<bool>(std::get<std::shared_ptr<Function>>(callback.data)) &&
             static_cast<bool>(std::get<std::shared_ptr<Function>>(callback.data)->invoke));
        if (!valid) throw std::runtime_error("events.on expects a function callback");
        std::lock_guard<std::mutex> lock(state->mutex);
        const uint64_t id = state->nextId++;
        state->listeners[name].push_back({id, callback, false});
        return Value(static_cast<int64_t>(id));
    });
    out->fields["once"] = native([state](const Args& args) {
        need(args, 2, "events.once");
        const std::string name = stringArg(args, 0, "events.once");
        const Value callback = args[1];
        const bool valid = std::holds_alternative<NativeFunction>(callback.data) ||
            (std::holds_alternative<std::shared_ptr<Function>>(callback.data) &&
             static_cast<bool>(std::get<std::shared_ptr<Function>>(callback.data)) &&
             static_cast<bool>(std::get<std::shared_ptr<Function>>(callback.data)->invoke));
        if (!valid) throw std::runtime_error("events.once expects a function callback");
        std::lock_guard<std::mutex> lock(state->mutex);
        const uint64_t id = state->nextId++;
        state->listeners[name].push_back({id, callback, true});
        return Value(static_cast<int64_t>(id));
    });
    out->fields["off"] = native([state](const Args& args) {
        need(args, 1, "events.off");
        int64_t id;
        if (auto p = std::get_if<int64_t>(&args[0].data)) id = *p;
        else throw std::runtime_error("events.off expects a listener ID returned by on() or once()");
        std::lock_guard<std::mutex> lock(state->mutex);
        for (auto it = state->listeners.begin(); it != state->listeners.end(); ++it) {
            auto& list = it->second;
            auto found = std::find_if(list.begin(), list.end(), [id](const Listener& l) { return l.id == static_cast<uint64_t>(id); });
            if (found != list.end()) {
                list.erase(found);
                if (list.empty()) state->listeners.erase(it);
                return Value(true);
            }
        }
        return Value(false);
    });
    out->fields["emit"] = native([state](const Args& args) {
        need(args, 1, "events.emit");
        const std::string name = stringArg(args, 0, "events.emit");
        Args payload(args.begin() + 1, args.end());
        std::vector<Listener> snapshot;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto it = state->listeners.find(name);
            if (it == state->listeners.end() || it->second.empty()) return Value(false);
            snapshot = it->second;
            auto& live = it->second;
            live.erase(std::remove_if(live.begin(), live.end(), [](const Listener& l) { return l.once; }), live.end());
            if (live.empty()) state->listeners.erase(it);
        }
        for (const auto& listener : snapshot) invokeListener(listener.callback, payload);
        return Value(true);
    });
    out->fields["listenerCount"] = native([state](const Args& args) {
        need(args, 1, "events.listenerCount");
        const std::string name = stringArg(args, 0, "events.listenerCount");
        std::lock_guard<std::mutex> lock(state->mutex);
        auto it = state->listeners.find(name);
        return Value(static_cast<int64_t>(it == state->listeners.end() ? 0 : it->second.size()));
    });
    out->fields["removeAllListeners"] = native([state](const Args& args) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (args.empty()) {
            size_t removed = 0;
            for (const auto& pair : state->listeners) removed += pair.second.size();
            state->listeners.clear();
            return Value(static_cast<int64_t>(removed));
        }
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("events.removeAllListeners expects an optional event name string");
        auto it = state->listeners.find(std::get<std::string>(args[0].data));
        if (it == state->listeners.end()) return Value(int64_t(0));
        const size_t removed = it->second.size();
        state->listeners.erase(it);
        return Value(static_cast<int64_t>(removed));
    });
    out->fields["eventNames"] = native([state](const Args& args) {
        if (!args.empty()) throw std::runtime_error("events.eventNames expects no arguments");
        auto names = makeArray();
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const auto& pair : state->listeners) if (!pair.second.empty()) names->items.emplace_back(pair.first);
        return Value(names);
    });
    return Value(out);
}

} // namespace astra
