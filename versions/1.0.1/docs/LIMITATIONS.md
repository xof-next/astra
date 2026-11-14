# v1.0.1 runtime limitations

This release is a small tree-walk interpreter with a native C++ standard-library subset. It is not a complete language implementation, a native machine-code compiler, or production-stable runtime.

Implemented in this expanded preview:

- Promise-based HTTP and TLS client requests through callable `launch(url)` (default GET) and `launch.get/post/put/patch/delete/head/options`, using libcurl on a bounded worker executor with response/time/size limits, redirect controls, and TLS certificate/hostname validation enabled by default.
- A bounded-worker HTTP/1.x server with common route methods, path parameters, bounded header/request/response sizes, an absolute request-read deadline, overload rejection, and an enforced `Server: Astra` response header. Route callback execution remains serialized to protect shared interpreter environments.
- Promise-based TLS verification, DNS lookup/resolve/reverse, and TCP connect/read/write operations with connect and socket timeouts, byte-string I/O, and explicit close.
- `async fn` / `pub async fn`, asynchronous arrow functions, promise values, `await`, and backward-compatible `sync fn` / `pub sync fn` execution contexts. Ordinary functions and top-level code reject `await`.
- TLS backend/policy information and client certificate verification through `tls`.
- Synchronous in-process event listeners through `events`.
- Gzip/zlib compression and CRC-32 with bounded decompression.
- Byte-string, query-string and common input-validation helpers.
- Named functions, JavaScript-style `function` declaration alias, expression/block arrow functions, basic lexical closures, and Rust-style synchronous closures (`|value| expression`).
- Core standard namespaces globally available without `import`; local declarations shadow these names lexically.
- CLI `install`, `update`, `version`, global bootstrap via `astra init` from a source checkout, `init --project`, task aliases in `[devDependencies]`, direct filename shorthand, `run`, `check`, and `modules` commands.

Not implemented in this preview:

- Native machine-code generation, bytecode, optimizer, debugger or profiler.
- External package installation, package registry, dependency resolution, `Astra.lock` generation, and loading `@scope/package` or local `./module` imports.
- A JavaScript-compatible event loop, promise chaining methods such as `.then()` and `.catch()`, UDP sockets, WebSocket, HTTP keep-alive, chunked request decoding, HTTP/2, HTTP/3, TLS server sockets, client certificates and custom TLS callbacks. Asynchronous functions and native HTTP, DNS and TCP operations use a bounded executor with at most 16 active tasks; additional work runs inline when the limit is reached. `await` waits for the current task result and unwraps nested future results. Shared mutable arrays and objects should not be mutated concurrently across asynchronous tasks.
- `switch`, `break`, `continue`, classes, structs, generics, typed function signatures, advanced closure semantics beyond basic lexical capture, and destructuring.
- Full static type checker, complete parser diagnostics, full stack traces and source maps.
- Method-style array calls such as `items.push(...)`, and full JavaScript coercion/short-circuit behavior.
- Full Unicode-aware string operations. Current basic string helpers and `std:format` case conversion operate on UTF-8 bytes / ASCII case rules.
- Sandboxed filesystem access or secure random-number generation. The HTTP server supports concurrent connection I/O through a bounded worker pool, but not parallel route callback execution. It is not a complete production web server; use a reverse proxy and OS-level limits before exposing it publicly.

`astra check` performs lexical checks and selected checks for standard module names/exports, built-in global module members, and constant reassignment. `astra run` executes only the supported interpreter subset. The CLI does not compile `.astra` source into a standalone native program.

## Runtime updater readiness

`astra install` and `astra update` are implemented against the future `xof-next/astra` GitHub Releases endpoint. The repository has not been published yet, so the updater cannot download a release in this snapshot. It requires per-platform executable release assets and SHA-256 digests/checksum manifest; see [CLI.md](CLI.md). These commands do not install project dependencies.

The runtime installs as one Astra executable in the user's global bin directory. Ordinary application projects only need `.astra` files and optionally `Astra.toml`; C++ source, CMake, compiler headers and standard-library source files are not copied into each project. A platform binary can still rely on native operating-system libraries unless its release build links those dependencies statically.

Build requirements for developing Astra itself: C++17, CMake, libcurl with an SSL/TLS backend, and zlib. Python 3 and the OpenSSL command-line program are optional test tools.

## Runtime safety bounds in this snapshot

The runtime applies explicit bounds to the most allocation-heavy standard-library paths. These bounds reduce accidental or untrusted-input exhaustion; they are not a substitute for operating-system sandboxing or process-level resource limits.

- JSON input and serialization output are capped at 64 MiB. JSON parsing/serialization nesting is limited to 128 levels, and parsed arrays are limited to 1,000,000 items.
- CSV input, filesystem text reads/writes, HTML transformations, encoded/decoded byte strings and compression/decompression paths use 64 MiB limits where applicable.
- `launch` defaults to a 10 MiB response cap; callers may raise `maxResponseBytes` up to 64 MiB. The default request timeout is 15 seconds and the default connection timeout is 5 seconds. The HTTP server defaults to loopback (`127.0.0.1`), a 1 MiB request body cap, a 4 MiB response body cap, a 64 KiB header cap and a 10-second absolute request-read deadline and socket inactivity timeout. `tcp.connect` defaults to a 5-second connect/read/write timeout.
- Collection transforms and directory listings cap item counts at 1,000,000. `collections.flatten` also limits its depth argument to 32.
- Interpreter loops stop after 1,000,000 iterations. Array and string indexes must be finite integral values within the signed 64-bit range.
- JSON serialization and string conversion reject excessive recursive nesting. Execution shutdown tracks lexical scopes and releases cyclic array/object/closure references created by the script.

These are implementation limits, not language guarantees; they may change in a later version. The interpreter still runs with the permissions of its host process, so only execute source code you trust.
