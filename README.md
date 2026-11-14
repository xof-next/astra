# Astra Script

Astra is a C++17 language runtime with a JavaScript-inspired syntax, a native command-line interface, and a built-in standard library. The repository keeps each runtime version in its own source directory so that older versions remain available and reproducible.

## Repository structure

```text
astra/
├── pack.json
├── README.md
├── scripts/
│   └── validate-pack.py
└── versions/
    ├── 1.0.0/
    └── 1.0.1/
        ├── CMakeLists.txt
        ├── src/
        ├── include/
        ├── stdlib/
        ├── config/
        ├── scripts/
        ├── examples/
        ├── tests/
        └── docs/
```

The root `pack.json` file is the authoritative version manifest. Its `new` field selects the default version for `astra install` and `astra update`. Runtime configuration through `astra.conf` is introduced in 1.0.1. Each version retains its own interpreter and language behavior while receiving platform-specific updater maintenance.

## Quick start

Astra must be bootstrapped from source when no Astra binary is already installed. Install the build dependencies for your operating system, then run:

```sh
git clone https://github.com/xof-next/astra.git
cd astra/versions/1.0.1
./scripts/build.sh
./build/astra init
```

For Windows, follow the Visual Studio and vcpkg instructions in [`versions/1.0.1/docs/INSTALLATION.md`](versions/1.0.1/docs/INSTALLATION.md), then run the generated `astra.exe init` command from PowerShell.

After installation:

```sh
astra --version
astra --help
astra init --project
```

See the [Astra 1.0.1 README](versions/1.0.1/README.md) for project setup, language examples, the HTTP server API, platform instructions, and runtime limits. The [runtime configuration reference](versions/1.0.1/docs/CONFIGURATION.md) documents all `astra.conf` settings and version compatibility.

## Version management

```sh
astra versions
astra install 1.0.0
astra install 1.0.1
astra use 1.0.1
astra update
```

`astra install` without a version installs the version selected by `pack.json` → `new`. `astra use` switches the active runtime without removing other installed versions. Online installation requires a published GitHub Release with the exact tag and platform asset names listed in `pack.json`, plus SHA-256 metadata accepted by the updater.

## Release process

1. Add a new source directory such as `versions/1.1.0/`; do not overwrite an existing version directory.
2. Add the version, release tag, source path, status, and seven platform asset names to `pack.json`, then set `new` to that version.
3. Push the complete repository to `main`. The commit includes every historical and current source directory, documentation, tests, manifest, and workflow.
4. GitHub Actions validates every registered source version and builds the seven assets only for the version selected by `pack.json` → `new`.
5. The workflow publishes or refreshes that version's GitHub Release and attaches `SHA256SUMS.txt`. Preview tags follow the newest preview source commit; stable source changes require a new version.
6. Confirm that every matrix job and release upload succeeds before running `astra install` or `astra update`.

## Current release status

Version `1.0.1` is the newest source version in the manifest. Version `1.0.0` remains available. The repository keeps the complete source tree for every manifest version. The release workflow builds the version selected by `pack.json` → `new` across Linux x86-64 and ARM64, macOS x86-64 and ARM64, Windows x86-64 and ARM64, and Android ARM64 for Termux. The workflow must complete successfully on each target before its uploaded binary is considered available. The manifest keeps 1.0.1 in preview status until maintainers promote it to stable.
