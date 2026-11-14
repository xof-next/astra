# Changelog

## Astra Script v1.0.1 — bounded concurrency and portability update

### HTTP server

- Replaced the sequential accept-and-process loop with a fixed-size worker pool.
- Added configurable `workers`, `queueCapacity`, and `backlog` listener options.
- Added bounded overload behavior: full pending queues receive HTTP 503 with `Retry-After: 1`.
- Added configurable maximum header, request-body, and response-body sizes.
- Added one absolute request-read deadline for headers and body, reducing slow-client worker starvation from byte-by-byte trickle traffic.
- Added HTTP 500 fallback when a route attempts to emit a response larger than `maxResponseBytes`.
- Added serialized route callback execution to protect shared mutable interpreter environments from concurrent evaluation races.
- Added orderly queue draining and worker joining when the listener stops.
- Added SIGINT and SIGTERM stop handling and platform-specific socket send protections.
- Kept one request per connection and fixed-length request bodies; keep-alive, chunked request bodies, TLS server sockets, HTTP/2 and HTTP/3 remain unsupported.

### Runtime configuration

- Added the optional `astra.conf` runtime configuration format for server limits and outbound HTTP client defaults.
- Added strict parsing with duplicate-key, unknown-key, syntax, unit, type, and range validation.
- Added bounded total and per-IP active connection counts, fixed-window total and per-IP request-arrival limits, and a bounded per-IP tracking table.
- Added server request, response, and header buffer defaults plus request deadlines through configuration.
- Added outbound `std:launch` request, response, and header buffer defaults, timeout settings, redirect limits, and TLS verification defaults.
- Preserved per-call options as the highest-precedence configuration layer.
- Added `astra config check` and project initialization of a safe `astra.conf` template.
- Kept the feature isolated to version 1.0.1; the 1.0.0 source, manifest version entry, and runtime behavior are unchanged.

### Asynchronous runtime

- Added promise-backed `async fn` and asynchronous arrow functions.
- Implemented `await` resolution for nested asynchronous values and runtime error propagation into `try`/`catch`.
- Moved outbound HTTP, TLS verification, DNS, TCP connect, TCP read, and TCP write operations onto a bounded worker executor.
- Preserved `sync fn` / `pub sync fn` and the existing `await` syntax for backward compatibility.
- Added concurrency-safe environment bindings, bounded asynchronous task creation, and shutdown waiting for outstanding tasks.

### Build and portability

- Added CMake thread-library discovery and portable `Threads::Threads` linkage.
- Updated the CMake project and runtime manifest version to 1.0.1.
- Added source-build instructions for Ubuntu, Debian, other Linux distributions, Windows, macOS, and Android through Termux.
- Added an Android NDK cross-build for arm64-v8a; execution on individual Termux devices still requires device-level validation.

### Tests and documentation

- Added an integration smoke test that sends 1,000 HTTP requests through 64 concurrent clients.
- Added runtime configuration integration tests for server rate/connection limits, request and response buffers, per-call overrides, and invalid configuration rejection.
- Expanded the server API reference with listener limits, request deadlines, bounded response behavior, overload semantics, safety notes, and production deployment guidance.
- Added platform-specific installation and build instructions.
- Updated runtime limitations to distinguish concurrent socket processing from serialized interpreter callback execution.

### Verification status

- The Debian GNU/Linux 13 x86_64 Release build succeeds in the current validation environment.
- All 39 CTest cases pass in the current Debian 13 x86_64 environment, including an absolute request-deadline check and an oversized-response check.
- Windows, macOS, other Linux libc variants, and Android/Termux have source-build instructions but were not executed in this validation environment.
- The 1,000-request concurrent integration test is a functional stress smoke test, not a throughput benchmark or a million-requests-per-second certification.
- The release workflow fixes the Android SDK package/license setup, Windows `min`/`max` macro collision, and macOS `timeval` narrowing errors identified in the previous runner logs.

---

# Changelog

## Astra Script v1.0.0 — initial versioned release

- Introduced root-repository `pack.json` version registry with `new: 1.0.0`.
- Added version-specific runtime storage and `astra install [version]`, `astra versions`, and `astra use`.
- Kept prior networking, multi-route HTTP server, standard library, and CLI improvements in the 1.0.0 source release.

### Included runtime changes

### Standard library

- Added `std:html` for HTML entity escaping and decoding.
- Added `std:mime` for common MIME type and extension lookup.
- Added `std:cookie` for Cookie-header parsing and Set-Cookie serialization.
- Added `std:collections` for `unique`, `chunk`, `flatten`, `zip` and `groupBy`.
- Added `std:server` for multi-route HTTP/1.x server handling with `Server: Astra` enforced on every response.
- Added `std:dns` for DNS/IP lookup and `std:tcp` for timeout-bounded TCP client connections.
- Expanded the MIME database to 135 common extensions and content types.
- The runtime now exposes 37 built-in modules and 172 registered exports.

### Runtime hardening

- Tightened JSON number, escape and Unicode-surrogate validation; added parser and serializer size/depth bounds.
- Reworked CSV parsing for quoted multiline fields and strict quote handling.
- Tightened Base64, hexadecimal and URL decoding validation.
- Added explicit limits to filesystem text operations and allocation-heavy collection helpers.
- Prevented unsafe floating-point-to-integer and array-index conversions at signed 64-bit boundaries.
- Added cleanup for cyclic arrays, objects, function closures and lexical environments at execution shutdown.
- Replaced the UUID v4 byte source with `std::random_device`; this is not a cross-platform guarantee of cryptographic randomness.
- Added task-aware `astra <name>`, direct `astra <file.astra>`, extensionless `astra run <name>`, and safe `[devDependencies]` task dispatch without shell execution.
- `astra init` now installs the current executable globally when invoked from the Astra source root; `astra init --project` explicitly creates an application project.
- Added `sync fn` / `pub sync fn` and restricted `await` syntax. `await` is a completed-value pass-through in this synchronous preview, not an async scheduler.

### Verification

- 37 CTest cases pass in the Release build, including HTTP server routes, TCP client integration, global installation, DNS and task aliases.
- Local HTTP client, HTTP server, TCP and TLS integration tests are included.
- The Linux Release build is verified; Windows/macOS builds were not verified in this environment.

This remains a C++17 interpreter preview, not a native-code compiler or a production sandbox. The HTTP server improves concurrent connection handling but does not provide parallel route callback execution, HTTP keep-alive, chunked decoding, HTTP/2, HTTP/3, or TLS server sockets. The manifest-based updater requires matching GitHub Release assets to be published.
