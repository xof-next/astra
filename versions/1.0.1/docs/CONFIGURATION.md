# Runtime Configuration

Astra 1.0.1 introduces the optional `astra.conf` file for runtime limits and network-client defaults. The file uses a small INI-style format with named sections and `key = value` entries. It is parsed by the native runtime and does not require an external configuration package.

`astra.conf` is not part of Astra 1.0.0. The 1.0.0 runtime neither reads nor generates this file. The configuration behavior described here applies only to 1.0.1 and later versions that explicitly document support for this format.

## File discovery

The runtime checks for `astra.conf` in the process working directory. Set `ASTRA_CONFIG` to use a different file. The environment variable accepts an absolute path or a path relative to the process working directory.

```sh
astra config check
ASTRA_CONFIG=/etc/my-service/astra.conf astra run main.astra
```

A missing file is not an error: built-in safe defaults are used. When a file is present, invalid syntax, duplicate keys, unknown keys, invalid types, and values outside supported ranges stop configuration loading with an error. Settings are loaded once per process and are not reloaded while the process is running.

`astra init --project` creates an `astra.conf` with conservative defaults. The version source directory also contains a reference `astra.conf`.

## Complete reference

```ini
[server]
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
```

### `[server]`

| Setting | Built-in default | Supported range | Meaning |
|---|---:|---|---|
| `host` | `127.0.0.1` | Non-empty hostname or IP | Local address to bind. Loopback is the safe default. |
| `workers` | Automatic, capped at 32 | `0` or `1`–`128` | `0` selects the runtime's automatic worker count. |
| `queue_capacity` | `workers × 64` | `1`–`65536` | Maximum waiting sockets, excluding sockets already assigned to workers. |
| `backlog` | `256` | `1`–`65535` | Requested OS listen backlog. The operating system may clamp it. |
| `max_connections` | `workers + queue_capacity` | `1`–`65536` | Maximum accepted sockets currently queued or being handled. |
| `max_connections_per_ip` | `64` | `1`–`65536` | Maximum accepted sockets queued or active for one observed peer IP. |
| `max_request_buffer` | `1mb` | `1b`–`64mb` | Maximum HTTP request body accepted by the server. |
| `max_response_buffer` | `4mb` | `1b`–`64mb` | Maximum response body accepted by response methods. |
| `max_header_buffer` | `64kb` | `1kb`–`1mb` | Maximum accumulated incoming request-header bytes. |
| `request_timeout` | `10s` | `100ms`–`120s` | Absolute deadline for receiving the request headers and body. |
| `max_tracked_ips` | `16384` | `128`–`1048576` | Upper bound for per-IP limiter state retained by the server. |

`max_connections` includes both queued sockets and sockets currently assigned to workers. It is not a limit on the number of clients that may ever connect during the process lifetime. `max_connections_per_ip` uses the socket peer address. When Astra is behind a reverse proxy, the peer address is normally the proxy address; this version does not trust forwarded client-IP headers for security decisions.

### `[rate_limit]`

| Setting | Default | Supported range | Meaning |
|---|---:|---|---|
| `enabled` | `true` | `true` or `false` | Enables connection-arrival rate limits. Connection limits remain active when this is false. |
| `window` | `1s` | `100ms`–`60s` | Fixed counting window. |
| `max_requests_total` | `5000` | `1`–`10000000` | Maximum accepted requests/connections per window across the server. |
| `max_requests_per_ip` | `2000` | `1`–`1000000` | Maximum accepted requests/connections per window from one peer IP. |

This server currently handles one HTTP request per TCP connection and closes the connection after the response. Therefore, these counters measure accepted request-bearing connections in the current implementation, not multiple requests multiplexed over one keep-alive connection. The limiter uses fixed windows rather than a sliding-window algorithm. Exceeding a rate limit returns HTTP `429 Too Many Requests`; connection or queue capacity exhaustion returns HTTP `503 Service Unavailable`.

### `[client]`

These values set defaults for the outbound HTTP functions in `std:launch`. A request options object may override the supported values for an individual call.

| Setting | Default | Supported range | Meaning |
|---|---:|---|---|
| `max_request_buffer` | `10mb` | `1b`–`64mb` | Maximum outbound HTTP request body. |
| `max_response_buffer` | `10mb` | `1b`–`64mb` | Maximum accumulated outbound HTTP response body. |
| `max_header_buffer` | `256kb` | `1kb`–`1mb` | Maximum accumulated response-header bytes. |
| `timeout` | `15s` | `100ms`–`120s` | Total outbound HTTP operation timeout. |
| `connect_timeout` | `5s` | `100ms`–`60s` | Connection establishment timeout. |
| `max_redirects` | `5` | `0`–`20` | Maximum followed redirects. |
| `verify_tls` | `true` | `true` or `false` | Enables certificate-chain and hostname verification. Keep enabled for normal use. |

Example per-call overrides:

```astra
const response = launch.get("https://example.com/data", {
    timeoutMs: 5000,
    maxResponseBytes: 2097152,
    maxHeaderBytes: 131072
});

const uploaded = launch.post("https://example.com/upload", payload, {
    maxRequestBytes: 4194304,
    verifyTls: true
});
```

## Precedence

For server listener values and outbound `launch.*` request values, precedence is:

1. Explicit options passed to `app.listen()` or `launch.*()` for keys supported by that API.
2. Values in `astra.conf`.
3. Built-in runtime defaults.

The `ASTRA_CONFIG` environment variable selects the configuration file; it does not override individual settings. Environment variables for individual options are not currently implemented.

## Syntax and units

- Section names and keys are case-insensitive; the documented lowercase spelling is recommended.
- String values may be enclosed in double quotes. The supported escapes are `\\` and `\"`.
- Boolean values are `true` or `false`.
- Integer settings must be whole decimal numbers.
- Sizes support `B`, `KB`/`KiB`, `MB`/`MiB`, and `GB`/`GiB` suffixes; binary multipliers are used. For example, `1mb` is 1,048,576 bytes.
- Durations support `ms`, `s`, and `m` with whole integer values, such as `1500ms`, `2s`, or `1m`. Fractional values such as `1.5s` are not supported.
- `#` and `;` begin a comment when they appear outside a quoted string.
- Duplicate keys and unknown settings are rejected to catch spelling mistakes.

## Operational guidance

Keep the default loopback bind unless remote clients must reach the process. When binding to `0.0.0.0`, put the service behind a firewall and, for public traffic, a maintained reverse proxy or load balancer. Keep request and response buffers close to the actual application requirement; large limits multiply memory pressure when many workers are simultaneously processing bodies.

The rate limiter and per-IP connection tracker are in-process controls, not a distributed edge protection system. They do not replace host firewall rules, reverse-proxy rate limits, DDoS mitigation, or capacity planning. Do not set limits to large values merely to avoid `429` or `503` responses. Load-test representative payloads and concurrency on the target host before changing production defaults.

## Version compatibility

`astra.conf` support is introduced in 1.0.1. Astra 1.0.0 does not read this file, does not use its settings, and does not create it from `astra init --project`. Keep version-specific project configuration when switching between installed runtimes; do not assume that a configuration option added in a later version will affect an older runtime.
