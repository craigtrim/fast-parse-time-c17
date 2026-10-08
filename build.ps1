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
$executable = Join-Path $buildPath 'fpt.exe'
$versionOutput = & $executable --version
if ($LASTEXITCODE -or $versionOutput -notmatch '^(?<Version>[0-9]+\.[0-9]+\.[0-9]+-c17\.[0-9]+)\s') {
    throw 'Could not read the built executable version.'
}
$version = $Matches.Version
$artifactVersion = if ($Configuration -eq 'Debug') { "$version-debug" } else { $version }
$executableName = "fpt-$artifactVersion.exe"
$versionedExecutable = Join-Path $buildPath $executableName
Copy-Item -LiteralPath $executable -Destination $versionedExecutable -Force
Write-Host "Executable: $versionedExecutable"
if ($Package) {
    $packageName = "fast-parse-time-c17-$artifactVersion"
    $packagePath = Join-Path $repoRoot "dist\$packageName"
    cmake --install $buildPath --prefix $packagePath
    if ($LASTEXITCODE) { throw 'Packaging failed.' }
    Copy-Item -LiteralPath $versionedExecutable -Destination (Join-Path $packagePath "bin\$executableName") -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $packagePath
    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs'), (Join-Path $repoRoot 'examples') -Destination $packagePath -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot 'src\generated\manifest.json') -Destination $packagePath
    $archivePath = Join-Path $repoRoot "dist\$packageName-windows-x64.zip"
    Compress-Archive -Path (Join-Path $packagePath '*') -DestinationPath $archivePath -Force
    Write-Host "Package: $archivePath"
}
