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
$sourcePath = Join-Path $root 'src\hamt.c'
$testSource = Join-Path $root 'tests\hamt_tests.c'
$objectDir = Join-Path $buildDir 'obj'
$pdbPath = Join-Path $buildDir 'hamt_tests.pdb'
$exePath = Join-Path $buildDir 'hamt_tests.exe'

New-Item -ItemType Directory -Force -Path $objectDir | Out-Null

if ($RunTests) {
    . $headlessTestHelper
    $null = Enable-HeadlessTestMode
}

& C:\Scriptorium\windows\Import-VisualCppEnvironment.ps1 -IncludePrerelease

$commonArgs = @(
    '/nologo',
    '/TC',
    '/std:c17',
    '/EHsc-',
    '/permissive-',
    '/W4',
    '/WX',
    '/wd4200',
    '/DTDS_HAMT_TESTING',
    "/I$includeDir",
    "/I$testSupportIncludeDir",
    "/Fo$objectDir\\",
    "/Fd$pdbPath",
    "/Fe:$exePath"
)

if ($Configuration -eq 'Debug') {
    $configurationArgs = @('/Od', '/Zi', '/MDd')
}
else {
    $configurationArgs = @('/O2', '/MD', '/DNDEBUG')
}

& cl.exe @commonArgs @configurationArgs $sourcePath $testSource
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed with exit code $LASTEXITCODE."
}

if ($RunTests) {
    & $exePath
    if ($LASTEXITCODE -ne 0) {
        throw "hamt_tests.exe failed with exit code $LASTEXITCODE."
    }
}
