# Astra built-in modules

Built-in modules are implemented in native C++ and imported with `std:`. The module inventory in `stdlib/modules.toml` is kept separate from the implementation files in `src/runtime/stdlib/`. The runtime rejects an unknown member with `E3002` instead of inventing an API.

## HTTP requests

```astra
import "std:launch" as launch;
import "std:json" as json;

pub async fn main() {
    let response = await launch.get("https://example.com/data");
    console.log(response.status, response.ok);
    let data = json.parse(response.body);
    console.log(data);

    let created = await launch.post(
        "https://example.com/items",
        json.stringify({ name: "Astra" }),
        { headers: { "Content-Type": "application/json" } }
    );
    console.log(created.status);
}
```

`std:launch` exports `request`, `get`, `post`, `put`, `patch`, `del`/`delete`, `head`, and `options`. The `launch` module is also callable: `launch(url, options)` is a GET request by default. `launch.request(url, options)` also defaults to GET when `options.method` is omitted. Every HTTP operation returns a promise value and executes on the bounded runtime task executor (up to 16 concurrent tasks). Use `await` inside `async fn` or the supported legacy `sync fn` form to receive the response. HTTP status codes such as 404 are returned normally in `response.status`; transport, timeout, protocol and TLS failures reject the promise and are raised at the await point.

`request(url, options)` supports `method`, `headers`, `body`, `timeoutMs`, `connectTimeoutMs`, `followRedirects`, `maxRedirects`, `maxRequestBytes`, `maxResponseBytes`, `maxHeaderBytes`, `verifyTls`, `userAgent`, and `caInfo` (a custom CA certificate bundle path). Request bodies must be strings; serialize JSON explicitly with `json.stringify()`. Defaults are supplied by the optional `[client]` section of `astra.conf` or the runtime defaults: 15-second total timeout, 5-second connect timeout, 10 MiB request and response caps, 256 KiB response-header cap, at most five redirects, TLS certificate and hostname verification enabled, and minimum TLS 1.2. Per-request options override matching configuration values. Redirects are restricted to HTTP and HTTPS. Set `verifyTls: false` only for controlled local testing; never use it for normal production traffic.

The HTTP implementation uses libcurl. CMake requires libcurl and zlib. The selected libcurl build must include an SSL/TLS backend for HTTPS.

## TLS information

```astra
import "std:tls" as tls;

pub fn main() {
    console.log(tls.supported());
    console.log(tls.backend());
    console.log(tls.policy());
    let result = await tls.verify("https://example.com");
    console.log(result.verified, result.status);
}
```

`tls.verify(url[, caInfo])` returns a promise and performs a HEAD handshake on the task executor with certificate-chain and hostname verification enabled. A successful result means the TLS peer verification succeeded; HTTP status alone does not determine TLS validity. The policy uses TLS 1.2 as its minimum protocol version.

## Events

```astra
import "std:events" as events;

fn onReady(message) {
    console.log("ready:", message);
}

pub fn main() {
    let listener = events.on("ready", onReady);
    events.emit("ready", "Astra started");
    events.off(listener);

    events.once("shutdown", onReady);
    events.emit("shutdown", "stopping");
}
```

Events are synchronous and delivered in registration order. `on` and `once` return listener IDs; `off(id)` removes one listener. `emit(name, ...args)` returns whether listeners were present. A listener registered with `once` is removed before invocation. This module does not provide background threads, timers, or asynchronous event queues.

## Data, compression and validation

| Module | API | Purpose |
|---|---|---|
| `std:buffer` | `byteLength`, `concat`, `slice`, `toHex`, `fromHex`, `toBase64`, `fromBase64` | Binary-safe byte strings and encodings |
| `std:compress` | `gzip`, `gunzip`, `deflate`, `inflate`, `crc32` | Gzip/zlib compression and CRC-32 |
| `std:query` | `parse`, `stringify`, `get`, `append` | URL query strings; repeated keys become arrays |
| `std:validate` | `isEmail`, `isUUID`, `isHex`, `isIPv4`, `isURL`, `isEmpty` | Common input checks |
| `std:html` | `escape`, `unescape` | HTML entity escaping and decoding |
| `std:mime` | `type`, `extension`, `isText` | Common MIME type lookup |
| `std:cookie` | `parse`, `serialize` | Cookie header and Set-Cookie helpers |
| `std:collections` | `unique`, `chunk`, `flatten`, `zip`, `groupBy` | Array collection transforms and grouping |
| `std:server` | `create` | HTTP server and multi-route registration |
| `std:dns` | `lookup`, `resolve`, `reverse`, `isIP` | Promise-based DNS and IP address helpers |
| `std:tcp` | `connect` | Promise-based TCP connection and socket I/O |

Compression output and decompression input/output are capped at 64 MiB to limit memory exhaustion. Buffer values are represented as strings in this interpreter preview; the functions operate on bytes, not Unicode grapheme clusters. The email and URL validators are pragmatic format checks, not complete standards validators.

## HTML, MIME and cookies

```astra
import "std:html" as html;
import "std:mime" as mime;
import "std:cookie" as cookie;

pub fn main() {
    console.log(html.escape("<script>&</script>"));
    console.log(mime.type("assets/app.js"));
    console.log(mime.extension("application/json"));

    let incoming = cookie.parse("sid=abc%20123; theme=dark");
    console.log(incoming.sid);
    let outgoing = cookie.serialize("sid", "abc 123", {
        path: "/",
        httpOnly: true,
        secure: true,
        sameSite: "Lax",
        maxAge: 3600
    });
    console.log(outgoing);
}
```

`html.escape()` encodes `&`, `<`, `>`, double quotes and apostrophes. `html.unescape()` supports the common named entities and numeric Unicode entities; unknown named entities are preserved. `mime.type(path)` returns `application/octet-stream` for unknown extensions. `mime.extension(type)` returns an extension including its leading dot, or an empty string if unknown. `mime.isText(type)` recognizes `text/*` plus common textual application types and SVG.

`cookie.parse(header)` decodes percent-escaped values and preserves repeated names as arrays. `cookie.serialize(name, value, options)` supports `domain`, `path`, `expires`, `maxAge`, `httpOnly`, `secure` and `sameSite`. `SameSite=None` requires `secure: true`. This API is a focused helper, not a complete browser cookie jar or a public-suffix validator.

`std:collections` preserves input order in `unique`, creates fixed-size subarrays with `chunk`, flattens nested arrays by a bounded depth (default one), pairs arrays to their shorter length with `zip`, and groups arrays of objects by a primitive property using `groupBy(items, "property")`. Ordinary string group keys remain directly addressable (for example, `groups.fruit`); non-string keys use type prefixes such as `#int:1` and `#bool:true` so an integer `1` cannot collide with the string `"1"`. Collection operations enforce item/depth limits to avoid unbounded allocations.

## Existing modules

| Module | API |
|---|---|
| `std:crypto` | `sha256`, `hmacSha256`, `timingSafeEqual` |
| `std:object` | `keys`, `values`, `entries`, `has`, `assign`, `remove` |
| `std:date` | `now`, `toISOString`, `year`, `month`, `day` |
| `std:math` | `abs`, `ceil`, `floor`, `round`, `sqrt`, `pow`, `min`, `max`, `clamp`, `sign`, `random` |
| `std:string` | `upper`, `lower`, `trim`, `contains`, `startsWith`, `endsWith`, `replace`, `split`, `repeat`, `reverse` |
| `std:array` | `length`, `push`, `pop`, `contains`, `join`, `reverse`, `sort` |
| `std:json` | `parse`, `stringify` |
| `std:time` | `now`, `sleep`, `measure` |
| `std:encoding` | `base64Encode`, `base64Decode`, `hexEncode`, `hexDecode`, `urlEncode`, `urlDecode` |
| `std:fs` | `readText`, `writeText`, `appendText`, `exists`, `remove`, `mkdir`, `list` |
| `std:path` | `join`, `basename`, `dirname`, `extension`, `normalize`, `isAbsolute` |
| `std:env` | `get`, `has`, `set` |
| `std:process` | `cwd`, `platform` |
| `std:random` | `int`, `float`, `bool`, `choice` |
| `std:uuid` | `v4` |
| `std:regex` | `test`, `match`, `replace` |
| `std:testing` | `assert`, `equal`, `notEqual` |
| `std:url` | `encode`, `decode`, `parse` |
| `std:csv` | `parse`, `stringify` |
| `std:console` | `log`, `info`, `warn`, `error`, `debug`, `table`, `clear` |
| `std:system` | `platform`, `arch`, `eol`, `cwd` |
| `std:format` | `padStart`, `padEnd`, `truncate`, `capitalize`, `camelCase`, `snakeCase`, `kebabCase` |
| `std:statistics` | `sum`, `mean`, `median`, `min`, `max`, `variance`, `standardDeviation` |

## Core namespaces available globally

The runtime preloads the following module namespaces into the global parent scope, so no import is required for common operations:

```astra
pub fn main() {
    console.log(math.max(3, 9));
    console.log(json.stringify({ ready: true }));
    console.log(crypto.sha256("hello"));
    console.log(events.listenerCount("ready"));
    console.log(tls.supported());
}
```

Global namespaces: `launch`, `server`, `dns`, `tcp`, `events`, `json`, `math`, `crypto`, `fs`, `path`, and `tls`. Explicit `import "std:..." as ...` remains supported. A project declaration such as `fn launch(url) { ... }` or `const launch = (url) => ...` shadows the global name in its lexical scope. It does not mutate the shared global module object. Module export objects are read-only; assigning `launch.get = ...` is rejected. Static checks and runtime checks both reject unknown members where the module identity is known.


## HTTP server

```astra
import "std:server" as http;
const app = http.create();
app.get("/", (req, res) => res.send("Astra"));
app.get("/users/:id", (req, res) => res.json({ id: req.params.id }));
app.post("/users", (req, res) => {
    res.status(201);
    res.json({ body: req.body });
});
app.route("PUT", "/users/:id", (req, res) => res.status(202).send(req.params.id));
app.listen(3000, { host: "127.0.0.1", workers: 8, queueCapacity: 512, backlog: 256, maxRequestBytes: 1048576, maxResponseBytes: 4194304, maxHeaderBytes: 65536, timeoutMs: 10000 });
```

Register as many routes as required before `listen()`. Supported method helpers are `get`, `post`, `put`, `patch`, `delete`, `head`, `options`, `all`, and generic `route(method, path, handler)`. Each route handler receives `(req, res)`. `req` exposes `method`, `url`, `path`, `body`, `headers`, `params`, and `query`. `res` exposes `status(code)`, `setHeader(name, value)`, `send(value)`, `end([value])`, and `json(value)`. The `Server` response header is reserved: the runtime always emits exactly `Server: Astra`, independent of application headers.

`listen(port, options)` accepts `host`, `workers`, `queueCapacity`, `backlog`, `maxConnections`, `maxConnectionsPerIp`, `maxRequestBytes`, `maxResponseBytes`, `maxHeaderBytes`, `timeoutMs`, and optional `maxRequests`. The default bind address is `127.0.0.1`; explicitly use `0.0.0.0` only when network access is intended. The optional `astra.conf` file supplies persistent defaults and also configures fixed-window total and per-IP request-arrival limits. Run `astra config check` to validate it; see [CONFIGURATION.md](CONFIGURATION.md). Astra 1.0.0 does not read or generate `astra.conf`. The server uses a bounded worker pool for concurrent connection I/O and rejects overload with HTTP 429 or 503 depending on the limit reached. Route callbacks are serialized to protect shared interpreter state. `timeoutMs` is an absolute deadline for receiving request headers and body, as well as the socket send/receive inactivity timeout. Response bodies larger than `maxResponseBytes` are rejected and converted to HTTP 500. It closes each connection after one request and rejects transfer-encoded/chunked request bodies. It is not a TLS server; put a reverse proxy in front of public services.

## DNS and TCP

```astra
import "std:dns" as dns;
import "std:tcp" as tcp;

pub async fn main() {
    console.log(await dns.resolve("localhost"));
    console.log(dns.isIP("127.0.0.1"));
    const socket = await tcp.connect("127.0.0.1", 9000, { timeoutMs: 5000 });
    await socket.write("ping");
    console.log(await socket.read(4096));
    socket.close();
}
```

`dns.lookup(host)` returns the first `{ address, family, version }` result; `dns.resolve(host)` returns up to 64 unique address records; `dns.reverse(ip)` performs a reverse lookup. `tcp.connect()` returns a promise that resolves to an object with promise-based `write(string)` and `read([maxBytes])`, plus synchronous `close()` and `isOpen()`. TCP has no built-in TLS layer.
