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

This remains a C++17 interpreter preview, not a native-code compiler or a production sandbox. The preview HTTP server is sequential and is not intended as a high-concurrency production server. The manifest-based updater is wired to `pack.json`; online binary installation requires the repository and matching GitHub Release assets to be published.

### Updater maintenance (Termux and version refresh)
- Detect Android ARM64 as `android-aarch64` instead of generic Linux ARM64.
- Install the active executable into `$PREFIX/bin/astra` on Termux when `PREFIX` is set.
- Make `astra update` download and verify the selected release even when that version is already installed.
- Keep `astra install <version>` and `astra use <version>` available for explicit version selection.
- Fix the local pack-manifest CLI test to follow the repository manifest's `new` version.
