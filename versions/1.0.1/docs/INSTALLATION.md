# Installation and Building Astra 1.0.1

Astra 1.0.1 is distributed as C++17 source. Build on the target operating system to use its native socket and system-library environment. Prebuilt release availability is separate from source portability.

## Requirements

- CMake 3.16 or newer.
- A C++17 compiler (GCC 9+, Clang 10+, or a supported recent MSVC toolset).
- libcurl development files with a TLS backend.
- zlib development files.
- CMake-compatible thread support.
- Python 3 for the complete integration test suite.

## Ubuntu and Debian

```sh
sudo apt update
sudo apt install -y build-essential cmake pkg-config libcurl4-openssl-dev zlib1g-dev python3

git clone https://github.com/xof-next/astra.git
cd astra/versions/1.0.1
./scripts/build.sh
./build/astra --version
```

The build script configures a Release build, compiles the runtime, and runs CTest. It uses two parallel compile jobs by default to avoid exhausting memory on small VPS instances and Android devices. Set `ASTRA_BUILD_JOBS` to a larger positive integer when the host has enough memory. To run those steps separately:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --parallel 2 --timeout 60
```

On Debian or Ubuntu minimal images, install the `pkg-config` package as well as the development libraries so CMake can discover system packages. If CMake selects an unexpected libcurl installation, inspect `CMakeCache.txt` and set `CMAKE_PREFIX_PATH` or the relevant CMake package variables for that host.

## Other Linux distributions

Install the equivalent compiler, CMake, pkg-config, libcurl, zlib, and Python 3 development/runtime packages using the distribution's package manager. Typical package names are:

- Fedora/RHEL family: `gcc-c++`, `cmake`, `pkgconf-pkg-config`, `libcurl-devel`, `zlib-devel`, `python3`.
- Arch family: `base-devel`, `cmake`, `pkgconf`, `curl`, `zlib`, `python`.

Then use the same CMake commands shown for Ubuntu/Debian. Both glibc and musl environments should be built and tested on the target image; a binary built against one libc is not automatically portable to the other.

## Windows

Install Visual Studio with the C++ desktop workload, CMake, and vcpkg. From a Developer PowerShell, install native dependencies and configure CMake using the vcpkg toolchain:

```powershell
vcpkg install curl:x64-windows zlib:x64-windows

git clone https://github.com/xof-next/astra.git
cd astra\versions\1.0.1
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
.\build\Release\astra.exe --version
```

For an ARM64 Windows toolchain, install matching ARM64 dependency triplets and configure the CMake generator/platform and vcpkg triplet accordingly. The x64 commands above are not an ARM64 cross-compile recipe.

## macOS

Install Xcode Command Line Tools and Homebrew dependencies:

```sh
xcode-select --install
brew install cmake pkg-config curl zlib

git clone https://github.com/xof-next/astra.git
cd astra/versions/1.0.1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --parallel 2 --timeout 60
```

If CMake does not discover Homebrew's curl or zlib, pass the appropriate Homebrew prefix through `CMAKE_PREFIX_PATH`.

## Android through Termux

This is a native Termux source build. The release pipeline cross-compiles a separate Android ARM64 executable with the Android NDK for the `arm64-v8a` ABI; it is not the desktop Linux ARM64 binary. The vcpkg Android triplet currently targets Android API 28. Install Termux from a maintained distribution source, open the Termux shell, and install the toolchain and library packages:

```sh
pkg update
pkg upgrade
pkg install clang cmake make pkg-config libcurl zlib python git

git clone https://github.com/xof-next/astra.git
cd astra/versions/1.0.1
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --parallel 2 --timeout 60
./build/astra --version
```

Use a low build parallelism value on phones with limited memory. Termux runs in Android's application environment and does not provide the same service-management or privileged networking model as a typical Linux server. Do not assume a Termux binary can be copied to Ubuntu, Debian, or another Android ABI environment.

## Install the built executable

After a successful build, run the global installer from the version source directory:

```sh
./build/astra init
```

On Windows:

```powershell
.\build\Release\astra.exe init
```

The installer places the runtime in the user-level Astra executable directory. On Termux, it uses `$PREFIX/bin/astra` when `PREFIX` is set; on other POSIX systems it uses `~/.local/bin/astra`. Verify the active binary with `astra --version` from a different working directory.

## Compatibility statement

The codebase includes native socket implementations for Windows Winsock and POSIX-style systems. CMake detects the host thread library and links it through `Threads::Threads`. This design is intended to build on common Linux distributions, Windows, macOS, and Android/Termux. It does not establish a guarantee for every CPU architecture, libc, compiler release, kernel, Android device, or package-manager configuration. Run the build and tests on each deployment target before declaring it supported in production.

## Runtime configuration

Astra 1.0.1 supports an optional `astra.conf` in the application working directory. `astra init --project` generates a safe starter file. Validate it with `astra config check` before deployment. Astra 1.0.0 does not read or generate `astra.conf`; retain version-specific project files when switching between installed runtimes. See [CONFIGURATION.md](CONFIGURATION.md) for supported settings and precedence.
