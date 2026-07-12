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
$hamtSourcePath = Join-Path $root 'src\hamt.c'
$patriciaSourcePath = Join-Path $root 'src\patricia.c'
$merkleSourcePath = Join-Path $root 'src\merkle_search_tree.c'
$testSource = Join-Path $root 'tests\hamt_tests.c'
$patriciaTestSource = Join-Path $root 'tests\patricia_tests.c'
$merkleTestSource = Join-Path $root 'tests\merkle_search_tree_tests.c'
$objectDir = Join-Path $buildDir 'obj'
$pdbPath = Join-Path $buildDir 'hamt_tests.pdb'
$exePath = Join-Path $buildDir 'hamt_tests.exe'
$patriciaPdbPath = Join-Path $buildDir 'patricia_tests.pdb'
$patriciaExePath = Join-Path $buildDir 'patricia_tests.exe'
$merklePdbPath = Join-Path $buildDir 'merkle_search_tree_tests.pdb'
$merkleExePath = Join-Path $buildDir 'merkle_search_tree_tests.exe'

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
    "/Fo$objectDir\\"
)

if ($Configuration -eq 'Debug') {
    $configurationArgs = @('/Od', '/Zi', '/MDd')
}
else {
    $configurationArgs = @('/O2', '/MD', '/DNDEBUG')
}

& cl.exe @commonArgs @configurationArgs "/Fd$pdbPath" "/Fe:$exePath" $hamtSourcePath $testSource
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed with exit code $LASTEXITCODE."
}

& cl.exe @commonArgs @configurationArgs "/Fd$patriciaPdbPath" "/Fe:$patriciaExePath" $patriciaSourcePath $patriciaTestSource
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed for the Patricia tests with exit code $LASTEXITCODE."
}

& cl.exe @commonArgs @configurationArgs "/Fd$merklePdbPath" "/Fe:$merkleExePath" $merkleSourcePath $merkleTestSource /link bcrypt.lib
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed for the Merkle search tree tests with exit code $LASTEXITCODE."
}

if ($RunTests) {
    & $exePath
    if ($LASTEXITCODE -ne 0) {
        throw "hamt_tests.exe failed with exit code $LASTEXITCODE."
    }

    & $patriciaExePath
    if ($LASTEXITCODE -ne 0) {
        throw "patricia_tests.exe failed with exit code $LASTEXITCODE."
    }

    & $merkleExePath
    if ($LASTEXITCODE -ne 0) {
        throw "merkle_search_tree_tests.exe failed with exit code $LASTEXITCODE."
    }
}
