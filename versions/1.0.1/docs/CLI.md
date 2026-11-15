# Astra CLI

## Commands

```sh
astra                         Start the interactive REPL
astra run                     Run the project entry from Astra.toml
astra run dev                 Run a named project script
astra run main.astra          Run a source file
astra file main.astra         Run a source file explicitly
astra main.astra              Run a source file directly
astra check main.astra        Check a source file without executing it
astra modules                 List built-in standard modules
astra config check            Validate astra.conf
astra init                    Install a local build or create a project
astra init --project [dir]    Create a project in the current/target directory
astra install [version]       Install a version from pack.json
astra i [version]             Alias for install
astra update                  Install and activate the newest version
astra u                       Alias for update
astra versions                List available and installed versions
astra v                       Alias for versions
astra use <version>           Activate an installed version
astra run                     Run a project or file
astra r [script|file]         Alias for run
astra c <file>                Alias for check
```

## Interactive REPL

Running `astra` without arguments always opens the interactive REPL. It does not execute a project entry automatically.

```text
Welcome to Astra v1.0.1.
Type ".help" for more information.

> let name = "Astra";
> name
Astra
> 2 + 3
5
> .exit
```

The REPL keeps variables and function declarations available between inputs. Incomplete blocks can continue on subsequent lines with the `...` prompt.

| REPL command | Purpose |
|---|---|
| `.help` | Show REPL commands |
| `.version` | Show the runtime version |
| `.clear` | Clear the terminal display |
| `.exit` | Leave the REPL |

## First-time global installation

Build the selected version from its source directory, then install that executable globally:

```sh
git clone https://github.com/xof-next/astra.git
cd astra/versions/1.0.1
ASTRA_BUILD_JOBS=1 bash scripts/build.sh
./build/astra init
```

Global executable locations:

- Linux/macOS: `~/.local/bin/astra`
- Android/Termux: `$PREFIX/bin/astra` when `PREFIX` is available
- Windows: `%LOCALAPPDATA%\Programs\Astra\bin\astra.exe`

On Linux/macOS, the installer prints a `PATH` line when `~/.local/bin` is not present in the current `PATH`. On Windows it prints the directory to add to the current user's `PATH`. Run `astra --version` from another directory to verify.

`astra init --project` creates an application project instead of installing the executable.

## Project scripts and file execution

Project commands are declared in the `[scripts]` section of `Astra.toml`. Package dependencies remain separate in `[dependencies]` and `[devDependencies]`.

```toml
[project]
name = "my-project"
version = "1.0.0"
entry = "src/main.astra"

[dependencies]

[devDependencies]

[scripts]
dev = "src/dev.astra"
test = "tests/main.astra"
```

Run the project entry and scripts:

```sh
astra run
astra run dev
astra run test
```

Run a file directly:

```sh
astra file src/main.astra
astra src/main.astra
astra run src/main.astra
```

A script value must resolve to an Astra source file. The CLI does not execute arbitrary shell commands from `Astra.toml`.

`astra init --project` creates `Astra.toml`, `astra.conf`, `main.astra`, `test.astra`, and `lib/test.astra`.

## Version manager

The repository-root `pack.json` is the version registry. Its `new` field identifies the default version, while each entry under `versions` records status, release tag, source path, and native asset names.

```sh
astra versions
astra install
astra install 1.0.0
astra i 1.0.1
astra use 1.0.0
astra update
```

`astra install` installs the version listed in `pack.json` → `new` when it is not already installed. If it is already installed, it activates that stored version. `astra update` downloads and verifies the newest release even when that version is already installed, then activates it. `astra use` activates an already-installed version, including an older version, without deleting the other versions.

The updater retrieves the selected GitHub Release, checks the platform asset and validates its SHA-256 digest before storing it. The active executable is replaced only after the downloaded version has passed validation.

Installed version files are stored independently:

- Linux/macOS/Android: `~/.astra/versions/<version>/astra`
- Windows: `%LOCALAPPDATA%\Astra\versions\<version>\astra.exe`

## Release workflow

When a commit changes one or more `versions/<version>/` directories on `main`, the release workflow builds every platform asset declared for each changed version. After all builds succeed, it replaces that version's GitHub Release and tag with assets generated from the commit. The previous release remains available if the build matrix fails before publication.
