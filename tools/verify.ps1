param(
    [string]$Source = (Join-Path $PSScriptRoot '..\..\fast-parse-time'),
    [string]$Python = (Join-Path $PSScriptRoot '..\.venv\Scripts\python.exe')
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = (Resolve-Path $Source).Path
$pythonPath = (Get-Command $Python -ErrorAction Stop).Source
Push-Location $repoRoot
try {
    & $pythonPath tools/check_source.py $sourcePath
    if ($LASTEXITCODE) { throw 'Source changed; update the port and regenerate its tables first.' }
    & (Join-Path $repoRoot 'build.ps1')
    # Upstream isolated subprocess tests intentionally use the installed package.
    & $pythonPath -m pip --disable-pip-version-check install --no-cache-dir --no-deps --no-build-isolation $sourcePath
    if ($LASTEXITCODE) { throw 'Could not install the source package for isolated upstream tests.' }
    & $pythonPath tests/run_upstream.py $sourcePath --mode baseline
    if ($LASTEXITCODE) { throw 'Upstream baseline failed.' }
    & $pythonPath tests/run_upstream.py $sourcePath --mode native
    if ($LASTEXITCODE) { throw 'Native upstream suite failed.' }
    & $pythonPath tests/compare_outcomes.py
    if ($LASTEXITCODE) { throw 'Upstream outcome parity failed.' }
    & $pythonPath tests/differential.py
    if ($LASTEXITCODE) { throw 'Output parity failed.' }
    & $pythonPath tests/extended_parity.py $sourcePath
    if ($LASTEXITCODE) { throw 'Knowledge-base/boundary parity failed.' }
    & $pythonPath tests/arithmetic_parity.py
    if ($LASTEXITCODE) { throw 'Arithmetic parity failed.' }
    & $pythonPath tests/cli_checks.py
    if ($LASTEXITCODE) { throw 'CLI verification failed.' }
    & $pythonPath tools/check_source.py $sourcePath
    if ($LASTEXITCODE) { throw 'Source changed during verification.' }
} finally {
    Pop-Location
}
