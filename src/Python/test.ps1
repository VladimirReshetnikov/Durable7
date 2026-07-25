# SPDX-License-Identifier: MIT-0

[CmdletBinding()]
param(
    [switch]$SkipInstall,
    [switch]$SkipPackageSmoke
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $workspace '..\..\eng\Enable-HeadlessTestMode.ps1')
$null = Enable-HeadlessTestMode

$launcher = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $launcher) { throw 'Python was not found on PATH.' }
$versionText = & $launcher.Source -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")'
if ($LASTEXITCODE -ne 0 -or [version]$versionText -lt [version]'3.11') {
    throw "Python 3.11 or newer is required; found $versionText."
}

$previousHashSeed = $env:PYTHONHASHSEED
$previousRayonThreads = $env:RAYON_NUM_THREADS
$previousCmakeParallelLevel = $env:CMAKE_BUILD_PARALLEL_LEVEL
$previousMakeFlags = $env:MAKEFLAGS
$env:PYTHONHASHSEED = '0'
$env:RAYON_NUM_THREADS = '1'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
$env:MAKEFLAGS = '-j1'
Push-Location $workspace

try {
    function Get-ValidatedWorkspaceChild([string] $Path) {
        $root = [IO.Path]::GetFullPath($workspace).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $full = [IO.Path]::GetFullPath($Path)
        if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to operate outside the Python workspace: $full"
        }
        return $full
    }

    $venv = Join-Path $workspace '.venv'
    # Python's venv layout is platform-specific even when the launcher is
    # PowerShell on both hosts. Keep one validated relative path and reuse it
    # for the developer environment and the clean installed-wheel smoke.
    $venvPythonRelativePath = if ($IsWindows) { 'Scripts\python.exe' } else { 'bin/python' }
    $python = Join-Path $venv $venvPythonRelativePath
    if (-not (Test-Path $python)) {
        python -m venv $venv
        if ($LASTEXITCODE -ne 0) { throw "Creating the Python validation environment failed." }
    }

    & $python -c 'import pip' 2>$null
    if ($LASTEXITCODE -ne 0) {
        & $python -m ensurepip --upgrade
        if ($LASTEXITCODE -ne 0) { throw "Bootstrapping pip in the validation environment failed." }
    }

    if (-not $SkipInstall) {
        & $python -m pip install --disable-pip-version-check --requirement requirements-dev.txt
        if ($LASTEXITCODE -ne 0) { throw "Installing Python validation dependencies failed." }
        & $python -m pip install --disable-pip-version-check --no-deps --editable .
        if ($LASTEXITCODE -ne 0) { throw "Installing the Python workspace failed." }
    }

    & $python -m ruff format --check .
    if ($LASTEXITCODE -ne 0) { throw "Ruff formatting validation failed." }

    & $python -m ruff check .
    if ($LASTEXITCODE -ne 0) { throw "Ruff lint validation failed." }

    & $python -m mypy
    if ($LASTEXITCODE -ne 0) { throw "Mypy validation failed." }

    & $python -m pytest -p no:cacheprovider
    if ($LASTEXITCODE -ne 0) { throw "Python tests failed." }

    $dist = Get-ValidatedWorkspaceChild (Join-Path $workspace 'dist')
    Remove-Item -LiteralPath $dist -Recurse -Force -ErrorAction SilentlyContinue
    & $python -m build
    if ($LASTEXITCODE -ne 0) { throw "Python package build failed." }

    & $python -m twine check dist\*
    if ($LASTEXITCODE -ne 0) { throw "Python package metadata validation failed." }

    if (-not $SkipPackageSmoke) {
        $smoke = Get-ValidatedWorkspaceChild (Join-Path $workspace 'build\package-smoke')
        Remove-Item -LiteralPath $smoke -Recurse -Force -ErrorAction SilentlyContinue
        & $launcher.Source -m venv $smoke
        if ($LASTEXITCODE -ne 0) { throw "Creating package-smoke environment failed." }

        $wheel = Get-ChildItem -LiteralPath $dist -Filter '*.whl' | Select-Object -First 1
        $smokePython = Join-Path $smoke $venvPythonRelativePath
        & $smokePython -m pip install --disable-pip-version-check $wheel.FullName
        if ($LASTEXITCODE -ne 0) { throw "Installing the built wheel failed." }

        $smokeScript = @'
from durable7 import (
    HashMapBulkBuilder,
    Interval,
    PersistentAssociation,
    PersistentDeque,
    PersistentHashBag,
    PersistentHashMultimap,
    PersistentBiMap,
    PersistentHashMap,
    PersistentIntervalMap,
    PersistentOrderedMap,
    PersistentOrderedSet,
    PersistentRelation,
    RangeUpdateSequence,
    UInt256,
)

class AdditiveRangeAlgebra:
    identity = 0
    identity_tag = 0

    def combine(self, left, right):
        return left + right

    def measure(self, element):
        return element

    def is_identity(self, tag):
        return tag == 0

    def compose(self, newer, older):
        return newer + older

    def apply_element(self, tag, element):
        return element + tag

    def apply_measure(self, tag, measure, count):
        return measure + tag * count


assert PersistentHashMap.empty().set("answer", 42).get("answer") == 42
factory = PersistentHashMap.empty().get_or_add("factory", lambda _key: 43)
assert factory.value == 43 and factory.map.get("factory") == 43
builder = HashMapBulkBuilder()
builder.set_item("built", 44)
assert builder.to_immutable().get("built") == 44
assert PersistentHashBag.from_values(["x", "x", "y"]).count_of("x") == 2
assert PersistentHashMultimap.empty().add("a", 1).contains("a", 1)
assert PersistentBiMap.empty().add("answer", 42).inverse[42] == "answer"
assert PersistentRelation.empty().add("a", 1).inverse.contains(1, "a")
assert PersistentDeque.from_iterable([1, 2]).append(3).to_list() == [1, 2, 3]
assert PersistentIntervalMap.empty().add(Interval(1, 2), "x")[Interval(1, 2)] == "x"
assert list(PersistentOrderedMap.empty().add("a", 1).keys()) == ["a"]
assert PersistentOrderedSet.from_values(["alpha", "beta", "alpha"]).to_list() == ["alpha", "beta"]
assert PersistentAssociation.from_pairs([("a", 1)]).get("a") == 1
range_sequence = RangeUpdateSequence.from_iterable([1, 2, 3], AdditiveRangeAlgebra())
assert range_sequence.apply_range(1, 2, 10).to_list() == [1, 12, 13]
assert int(UInt256(-1)) == 2**256 - 1
'@
        & $smokePython -c $smokeScript
        if ($LASTEXITCODE -ne 0) { throw "Installed-wheel smoke test failed." }
    }
}
finally {
    Pop-Location
    if ($null -eq $previousHashSeed) {
        Remove-Item Env:PYTHONHASHSEED -ErrorAction SilentlyContinue
    }
    else {
        $env:PYTHONHASHSEED = $previousHashSeed
    }
    if ($null -eq $previousRayonThreads) {
        Remove-Item Env:RAYON_NUM_THREADS -ErrorAction SilentlyContinue
    }
    else {
        $env:RAYON_NUM_THREADS = $previousRayonThreads
    }
    if ($null -eq $previousCmakeParallelLevel) {
        Remove-Item Env:CMAKE_BUILD_PARALLEL_LEVEL -ErrorAction SilentlyContinue
    }
    else {
        $env:CMAKE_BUILD_PARALLEL_LEVEL = $previousCmakeParallelLevel
    }
    if ($null -eq $previousMakeFlags) {
        Remove-Item Env:MAKEFLAGS -ErrorAction SilentlyContinue
    }
    else {
        $env:MAKEFLAGS = $previousMakeFlags
    }
}
