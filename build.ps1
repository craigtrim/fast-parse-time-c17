param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$buildPath = Join-Path $repoRoot 'build'
cmake -S $repoRoot -B $buildPath -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
cmake --build $buildPath --parallel
if ($LASTEXITCODE) { throw 'C17 build failed.' }
ctest --test-dir $buildPath --output-on-failure
if ($LASTEXITCODE) { throw 'Native tests failed.' }
if ($Package) {
    $packagePath = Join-Path $repoRoot 'dist\fast-parse-time-c17'
    cmake --install $buildPath --prefix $packagePath
    if ($LASTEXITCODE) { throw 'Packaging failed.' }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $packagePath
    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs'), (Join-Path $repoRoot 'examples') -Destination $packagePath -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'src\generated\manifest.json') -Destination $packagePath
    Compress-Archive -Path (Join-Path $packagePath '*') -DestinationPath (Join-Path $repoRoot 'dist\fast-parse-time-c17-windows-x64.zip') -Force
}
