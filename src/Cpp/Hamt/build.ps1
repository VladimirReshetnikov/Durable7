[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [switch] $RunTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$headlessTestHelper = Join-Path $root '..\..\..\eng\Enable-HeadlessTestMode.ps1'
$buildRoot = Join-Path $root 'build'
$buildDir = Join-Path $buildRoot $Configuration
$includeDir = Join-Path $root 'include'
$testSupportIncludeDir = Join-Path $root '..\..\test_support\include'
$testSource = Join-Path $root 'tests\persistent_hamt_tests.cpp'
$objectPath = Join-Path $buildDir 'persistent_hamt_tests.obj'
$pdbPath = Join-Path $buildDir 'persistent_hamt_tests.pdb'
$exePath = Join-Path $buildDir 'persistent_hamt_tests.exe'

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

if ($RunTests) {
    . $headlessTestHelper
    $null = Enable-HeadlessTestMode
}

& C:\Scriptorium\windows\Import-VisualCppEnvironment.ps1 -IncludePrerelease

$commonArgs = @(
    '/nologo',
    '/std:c++20',
    '/EHsc',
    '/permissive-',
    '/W4',
    '/WX',
    '/Zc:__cplusplus',
    "/I$includeDir",
    "/I$testSupportIncludeDir",
    "/Fo$objectPath",
    "/Fd$pdbPath"
)

if ($Configuration -eq 'Debug') {
    $configurationArgs = @('/Od', '/Zi', '/MDd')
}
else {
    $configurationArgs = @('/O2', '/MD', '/DNDEBUG')
}

& cl.exe @commonArgs @configurationArgs $testSource "/Fe:$exePath"
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed with exit code $LASTEXITCODE."
}

if ($RunTests) {
    & $exePath
    if ($LASTEXITCODE -ne 0) {
        throw "persistent_hamt_tests.exe failed with exit code $LASTEXITCODE."
    }
}
