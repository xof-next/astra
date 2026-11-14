# Astra HTTP Server

`std:server` provides a native HTTP/1.x server with route registration and a bounded worker pool. It is intended for application endpoints and controlled services. It is not a replacement for a hardened edge proxy, a TLS terminator, or a dedicated asynchronous network runtime.

## Runtime configuration

Listener defaults can be managed through `astra.conf` instead of repeating options in every `app.listen()` call. It includes bounded worker and queue settings, total and per-IP connection limits, fixed-window rate limits, request/response/header buffer ceilings, and request deadlines. Explicit listener options override the matching `.conf` values. Run `astra config check` to validate the file before deployment. See [CONFIGURATION.md](CONFIGURATION.md) for the complete schema, defaults, ranges, and examples.

The file is supported from Astra 1.0.1 onward. Astra 1.0.0 does not load or generate `astra.conf`.

## Minimal server

```astra
import "std:server" as http;

const app = http.create();

app.get("/", (req, res) => {
    res.send("Astra is running");
});

app.get("/users/:id", (req, res) => {
    res.json({ id: req.params.id, method: req.method });
});

app.listen(3000);
```

Run it with:

```sh
astra run main.astra
```

The default bind address is `127.0.0.1`, which accepts local connections only. Use `0.0.0.0` only when the service is intended to accept connections from other network interfaces and a firewall or reverse proxy is configured.

## Routes

Supported convenience methods are `get`, `post`, `put`, `patch`, `delete`, `head`, `options`, and `all`. `route(method, path, handler)` registers the same methods explicitly.

Route parameters use `:name` segments. The request object exposes:

- `req.method`: normalized uppercase method.
- `req.url`: request target, including its query string.
- `req.path`: path without the query string.
- `req.params`: decoded route parameters.
- `req.query`: decoded query key/value strings.
- `req.headers`: lowercase request-header names.
- `req.body`: request body as a string.

The response object exposes:

- `res.status(code)`: set a status from 100 through 599.
- `res.setHeader(name, value)`: set a response header. `Server` is reserved.
- `res.send(value)`: send a stringified body.
- `res.end([value])`: finish with an optional body.
- `res.json(value)`: serialize a value as JSON.

The server supplies `Content-Length`, a default text content type, `Connection: close`, and `Server: Astra`. Application code cannot override the `Server`, `Content-Length`, or `Connection` response headers.

## Listener options

All options are optional. The values below are validated before the socket starts listening.

| Option | Default | Allowed values | Purpose |
|---|---:|---:|---|
| `host` | `127.0.0.1` | Non-empty hostname or IP | Local bind address. Use `0.0.0.0` to bind IPv4 interfaces. |
| `workers` | Hardware threads, capped at 32; minimum default 2 | 1–128 | Number of worker threads processing accepted connections. |
| `queueCapacity` | `workers * 64` | 1–65536 | Maximum waiting connections, excluding workers currently processing connections. |
| `backlog` | 256 | 1–65535 | Requested operating-system listen backlog. The OS may clamp this value. |
| `maxConnections` | `workers + queueCapacity` | 1–65536 | Maximum accepted sockets queued or currently handled. |
| `maxConnectionsPerIp` | 64 | 1–65536 | Maximum queued or active sockets for one observed peer IP. |
| `maxRequests` | `0` | 0–2147483647 | Maximum accepted TCP connections before the server drains and stops. `0` means no configured limit. The current server handles one HTTP request per connection. |
| `maxRequestBytes` | 1048576 | 1–67108864 | Maximum accepted request body size in bytes. |
| `maxResponseBytes` | 4194304 | 1–67108864 | Maximum response body size accepted by `send`, `end`, or `json`. Oversized handler output becomes HTTP 500. |
| `maxHeaderBytes` | 65536 | 1024–1048576 | Maximum accumulated request-header bytes. |
| `timeoutMs` | 10000 | 100–120000 | Absolute deadline for receiving the request headers and body, plus socket send/receive inactivity timeout. |

Example for a server with more concurrent socket capacity:

```astra
app.listen(8080, {
    host: "0.0.0.0",
    workers: 16,
    queueCapacity: 2048,
    backlog: 1024,
    maxRequests: 0,
    maxRequestBytes: 4194304,
    maxResponseBytes: 8388608,
    maxHeaderBytes: 65536,
    timeoutMs: 15000
});
```

Choose worker and queue counts from the machine's CPU, memory, file-descriptor limit, and workload. A large queue does not create processing capacity; it retains more sockets and increases waiting time. `maxConnections` counts queued and worker-assigned sockets together. `maxConnectionsPerIp` uses the remote socket address, not forwarded headers. The `astra.conf` fixed-window rate limiter can also cap total and per-IP connection arrivals; details and defaults are in [CONFIGURATION.md](CONFIGURATION.md). When the queue or a connection cap is full, newly accepted connections receive HTTP `503 Service Unavailable` with `Retry-After: 1` and are closed; rate-limited peers receive HTTP `429`. This is bounded overload behavior, not an unlimited queue.

## Concurrency and interpreter safety

The accept loop can enqueue connections while a fixed number of workers read requests, match routes, and write responses. The worker count is fixed for the lifetime of the listener; the server does not create a thread for every incoming connection.

Astra route callbacks are protected by a per-server mutex. This is deliberate: handlers can capture and mutate shared interpreter environments, and the interpreter is not designed for concurrent evaluation of the same environment. Socket handling is concurrent, but route callback execution is serialized. As a result, this release improves concurrent connection handling and prevents an unbounded thread-per-connection design, but it does not provide parallel execution of CPU-heavy Astra handlers. A route that performs a slow blocking operation can delay other route callbacks.

The queue is bounded. Under overload the server rejects new work rather than allocating threads or queue entries without a limit. `maxRequests: 0` allows the accept loop to continue until the process receives a shutdown signal or is stopped externally. Request-header and request-body reads share one absolute deadline, so a client cannot keep a worker indefinitely by sending a small amount of data just before each inactivity timeout. On normal listener shutdown, queued work is drained and worker threads are joined.

## Request and protocol limits

This release supports HTTP/1.0 and HTTP/1.1 request lines, fixed `Content-Length` request bodies, multiple routes, and one request per TCP connection. It closes the connection after the response. The following are not implemented in this version:

- Persistent HTTP keep-alive and HTTP pipelining.
- `Transfer-Encoding: chunked` request bodies.
- HTTP/2, HTTP/3, QUIC, WebSocket upgrades, and TLS server sockets.
- Streaming request or response bodies.
- A non-blocking event loop or parallel Astra handler execution.

Unsupported transfer encodings are rejected. Oversized headers return `431`; oversized request bodies return `413`; malformed request headers return `400`; incomplete or deadline-expired requests can return `408`; oversized handler responses become `500`; and queue overload returns `503`.

## Production deployment guidance

Do not treat a high worker count or an unlimited `maxRequests` value as a promise of a particular requests-per-second rate. Actual capacity depends on route work, connection churn, payload sizes, CPU, memory, file descriptors, network bandwidth, kernel socket limits, and reverse-proxy behavior. The server has not been certified for a particular million-requests-per-second target.

For public services, use a maintained reverse proxy or load balancer in front of Astra for TLS termination, connection limits, request buffering, rate limiting, access logging, and edge protection. Keep Astra bound to a private interface when possible. Set operating-system file-descriptor and process limits deliberately. For sustained CPU-bound workloads, scale across multiple Astra processes or service instances behind a load balancer; do not increase `workers` indefinitely because route callbacks remain serialized.

## Basic request check

With the minimal server running:

```sh
curl -i http://127.0.0.1:3000/
curl -i http://127.0.0.1:3000/users/42
```

A concurrent smoke test can be run with a tool such as `wrk` against a dedicated test environment. Record the exact command, worker count, queue capacity, payload, concurrency, duration, CPU, memory, and error rate when comparing results. Do not benchmark an internet-facing production service without authorization.
