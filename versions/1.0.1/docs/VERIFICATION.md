# Astra 1.0.1 validation record

Validation date: 2026-11-12

## Verified environment

| Component | Environment |
|---|---|
| Operating system | Debian GNU/Linux 13 (trixie) |
| Kernel / architecture | Linux 6.18.57, x86_64 |
| C++ compiler | GCC 14.2.0 |
| CMake | 3.31.6 |
| Python | 3.13.5 |
| libcurl | 8.10.1 with OpenSSL 3.5.5 |
| zlib | 1.3.1 |

## Results

- Clean CMake Release configuration and compile completed successfully.
- CTest: 39 of 39 tests passed, including configuration parsing, rate-limit enforcement, server body-buffer enforcement, outbound client buffer enforcement, per-call overrides, and rejection of unknown settings.
- HTTP worker-pool stress smoke test: 1,000 requests through 64 concurrent clients completed successfully.
- The direct stress-test process completed in approximately 1.07 seconds in this environment; this includes server startup and shutdown and must not be treated as a controlled requests-per-second benchmark.
- The server integration suite verified route behavior, the request-read deadline, and the maximum response-body limit. Runtime configuration tests verified server rate limits and request-body limits, outbound HTTP client buffer limits, per-call overrides, and strict rejection of unknown keys.
- Repository pack validation completed successfully with two registered versions and `new` set to `1.0.1`.

## Platforms not verified in this record

Windows, macOS, Ubuntu, Debian architectures other than x86_64, additional Linux distributions and libc variants, Android NDK, and Android/Termux were not built or tested in this execution environment. The source and platform instructions provide intended build paths, not evidence that every OS and architecture has passed validation.

## Capacity and production qualification

The 1,000-request test validates a finite functional workload on local loopback. It does not establish million-request capacity, sustained requests per second, behavior under hostile traffic, memory stability over long-running soak tests, or performance on production VPS hardware. Route callbacks are serialized to protect shared interpreter state. A production qualification should include target-host soak tests, mixed payload sizes, overload tests, process restarts, OS descriptor limits, and load tests through the intended reverse proxy.
