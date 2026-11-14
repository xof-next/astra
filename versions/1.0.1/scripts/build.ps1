$ErrorActionPreference = 'Stop'
$buildJobs = 2
if ($env:ASTRA_BUILD_JOBS) {
    $parsedJobs = 0
    if (-not [int]::TryParse($env:ASTRA_BUILD_JOBS, [ref]$parsedJobs) -or $parsedJobs -lt 1) {
        throw 'ASTRA_BUILD_JOBS must be a positive integer.'
    }
    $buildJobs = $parsedJobs
}
$configureArgs = @('-S', '.', '-B', 'build', '-DCMAKE_BUILD_TYPE=Release')
if ($env:VCPKG_ROOT) {
    $toolchain = Join-Path $env:VCPKG_ROOT 'scripts/buildsystems/vcpkg.cmake'
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    if ($env:VCPKG_DEFAULT_TRIPLET) {
        $configureArgs += "-DVCPKG_TARGET_TRIPLET=$env:VCPKG_DEFAULT_TRIPLET"
    }
}
cmake @configureArgs
cmake --build build --config Release --parallel $buildJobs
ctest --test-dir build -C Release --output-on-failure --parallel $buildJobs --timeout 60
