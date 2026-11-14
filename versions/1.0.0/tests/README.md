# Test suite

The CTest suite checks CLI startup/version/help, built-in module registration, project-manifest execution, minimal project creation, basic language syntax, BigInt arithmetic, named functions, arrows, closures, global namespace shadowing, read-only module exports, diagnostics, integer overflow, and rejection of unknown module members.

Run from the project root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Some tests are intentionally invalid inputs and are marked as expected failures. `all-modules.astra` imports and invokes representative APIs across all 34 registered modules. `run_launch_test.py` starts a local HTTP server and tests GET, POST, HEAD, custom headers, redirects, status handling and response-size limits. If Python 3 and the OpenSSL CLI are available, `run_tls_test.py` creates a local test certificate and checks trusted certificate verification plus HTTPS-to-HTTP redirect blocking.

`closures.astra` verifies captured mutable state. `arrows-global.astra` exercises regular arrows, block arrows, Rust-style closures, zero-argument closures, built-in module globals and local shadowing. `run_init_test.py` verifies that `astra init` creates only `Astra.toml` and `main.astra`, and that the new project immediately runs.

The suite has been run under AddressSanitizer and UndefinedBehaviorSanitizer with leak detection enabled. The GitHub release updater is compile-checked but cannot be end-to-end tested until the `xof-next/astra` repository and its release assets exist.

Additional regression coverage:

- `web-tools.astra`: HTML entities, MIME lookup, cookies and invalid cookie options.
- `json-csv-hardening.astra`: JSON Unicode/number grammar/depth-safe parsing, multiline CSV, strict base64/URL decoding, numeric overflow, and cleanup of cyclic arrays/objects and captured closures.
- `collections.astra`: uniqueness, chunking, flatten depth, zip length, type-safe object grouping and primitive-key collision handling.
