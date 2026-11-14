# Test suite

The CTest suite checks CLI startup/version/help, built-in module registration, project-manifest execution, minimal project creation, basic language syntax, BigInt arithmetic, named functions, arrows, closures, global namespace shadowing, read-only module exports, diagnostics, integer overflow, and rejection of unknown module members.

Run from the project root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Some tests are intentionally invalid inputs and are marked as expected failures. `all-modules.astra` imports and invokes representative APIs across all 37 registered modules. `run_launch_test.py` starts a local HTTP server and tests GET, POST, HEAD, custom headers, redirects, status handling and response-size limits. If Python 3 and the OpenSSL CLI are available, `run_tls_test.py` creates a local test certificate and checks trusted certificate verification plus HTTPS-to-HTTP redirect blocking.

`closures.astra` verifies captured mutable state. `arrows-global.astra` exercises regular arrows, block arrows, pipe-parameter closures, zero-argument closures, built-in module globals and local shadowing. `run_init_test.py` verifies that `astra init` creates `Astra.toml`, `astra.conf`, source files, and a project that immediately runs.

The GitHub release updater is compile-checked but cannot be end-to-end tested until the `xof-next/astra` repository and its release assets exist. Sanitizer builds should be run on each supported compiler and target before a platform release is promoted.

Additional regression coverage:

- `web-tools.astra`: HTML entities, MIME lookup, cookies and invalid cookie options.
- `json-csv-hardening.astra`: JSON Unicode/number grammar/depth-safe parsing, multiline CSV, strict base64/URL decoding, numeric overflow, and cleanup of cyclic arrays/objects and captured closures.
- `collections.astra`: uniqueness, chunking, flatten depth, zip length, type-safe object grouping and primitive-key collision handling.

`run_server_load_test.py` sends 1,000 HTTP requests through 64 concurrent clients to verify bounded worker-pool request handling. `run_runtime_config_test.py` verifies configuration parsing, per-IP rate limiting, request-body limits, outbound client buffer limits, per-call overrides, and rejection of unknown keys. The server integration tests also verify absolute request-read deadlines and response-size enforcement. These are regression and stress smoke tests, not a throughput benchmark or a million-requests-per-second certification.


The `language_runtime_regressions` CTest invokes `run_language_regressions.py`. It covers block and lexical scope, shadowing, function hoisting and duplicate declarations, closures, destructuring and argument errors, `null`/`undefined`, optional property chaining, assignment mutability, `#` comments, `sync`/`async` functions, awaited values, imports, export aliases, live bindings, namespace/default/import-all forms, relative path resolution, successful circular imports, and diagnostic codes. The `language_runtime_example` and `module_runtime_example` CTest cases run the checked-in user examples.
