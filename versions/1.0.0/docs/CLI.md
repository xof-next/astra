# Astra CLI

## Commands

```sh
astra                         # project.entry from Astra.toml, otherwise main.astra
astra <name>                  # task alias, then <name>.astra, then exact file
astra <file.astra>            # run that source file
astra run [name|file]         # run source; extension is optional
astra check <file>            # lexical and selected static checks
astra modules                 # list built-in module exports
astra version
astra --version
astra init                    # global install when run in Astra source checkout
astra init --project [dir]    # create a project in current/target directory
astra install [version]
astra update
astra versions
astra use <version>
```

## First-time global installation

Clone the source repository and build the selected version once:

```sh
git clone https://github.com/xof-next/astra.git
cd astra/versions/1.0.0
./scripts/build.sh
./build/astra init
```

When `astra init` runs with no arguments from the Astra source root (detected by `CMakeLists.txt`, `src/cli/main.cpp`, and `stdlib/modules.toml`), it copies the currently running executable into the user's global executable directory. It does not copy source files into application projects.

- Linux/macOS: `~/.local/bin/astra`
- Windows: `%LOCALAPPDATA%\Programs\Astra\bin\astra.exe`

On Linux/macOS the installer prints a `PATH` line when `~/.local/bin` is not present in the current `PATH`. On Windows it prints the directory to add to the current user's `PATH`; open a new terminal after changing it. Run `astra --version` from another directory to verify.

The local installer stages the copy before activation, applies executable permissions on POSIX, refuses to replace a symbolic link or non-regular target, and does not run a shell command. `astra init --project` explicitly bypasses source-checkout detection and creates an application project instead.

## Project tasks and file shorthand

A project may declare task aliases in the `[devDependencies]` section of `Astra.toml`:

```toml
[project]
name = "my-project"
version = "1.0.0"
entry = "main.astra"

[devDependencies]
test = "astra lib/test.astra"
```

Then:

```sh
astra          # runs main.astra
astra test     # runs lib/test.astra through the declared task
astra test.astra
astra run test # resolves test.astra
astra run lib/test
```

Task aliases take priority over files with the same name. If no task matches, Astra resolves the exact supplied path, then adds `.astra` when the name has no extension. Task values are deliberately restricted to `astra <file>` or `astra run <file>`; the CLI does not execute manifest task values through a shell, so commands such as `rm`, pipes, redirects, or chained shell expressions are rejected. This is a small task runner, not an external package manager.

`astra init --project` creates:

```text
my-project/
├── Astra.toml
├── main.astra
├── test.astra
└── lib/
    └── test.astra
```

## Manifest-based version manager

The repository-root `pack.json` is the version registry. Its `new` field identifies the default version, while each key under `versions` records the release status, Git tag, immutable source path, and platform binary asset names.

```sh
astra versions              # read versions from pack.json
astra install               # install pack.json -> new
astra install 1.0.0         # install this exact release
astra use 1.0.0             # activate an already-installed version
astra update                # install and activate pack.json -> new
```

The updater reads:

`https://raw.githubusercontent.com/xof-next/astra/main/pack.json`

It then requests the matching GitHub Release tag (for example `v1.0.0`). Each selected version must have the platform asset named in its manifest entry. The updater verifies the SHA-256 digest from GitHub Release metadata, or from a `SHA256SUMS.txt` asset, before preserving the binary.

Installed binaries are kept independently:

- Linux/macOS: `~/.astra/versions/<version>/astra`
- Windows: `%LOCALAPPDATA%\Astra\versions\<version>\astra.exe`

The active command remains at the global CLI path. `astra use` stages and atomically replaces that active executable from the selected installed version. Other version directories are retained.

Expected asset names in the initial `1.0.0` manifest entry:

| Platform | Asset |
|---|---|
| Linux x86-64 | `astra-linux-x86_64` |
| Linux ARM64 | `astra-linux-aarch64` |
| macOS Intel | `astra-macos-x86_64` |
| macOS Apple Silicon | `astra-macos-aarch64` |
| Windows x86-64 | `astra-windows-x86_64.exe` |
| Windows ARM64 | `astra-windows-aarch64.exe` |

A source push alone does not publish executable release assets. The first bootstrap is therefore built from source; `astra install` and `astra update` become available after the matching GitHub Release and its verified platform assets have been published. The checksum verifies artifact integrity against the release metadata; it is not an independent publisher-signature system.
## Runtime layout

The CLI executable contains the interpreter and registered built-in modules. Individual projects need `.astra` source files and optionally `Astra.toml`; they do not need a C++ compiler, CMake, `include/`, `src/`, `stdlib/`, or copied runtime modules. This preview does not install external package dependencies.
