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
$visualCppHelper = Join-Path $root '..\..\..\eng\Import-VisualCppEnvironment.ps1'
$buildRoot = Join-Path $root 'build'
$buildDir = Join-Path $buildRoot $Configuration
$includeDir = Join-Path $root 'include'
$testSupportIncludeDir = Join-Path $root '..\..\test_support\include'
$hamtSourcePath = Join-Path $root 'src\hamt.c'
$bagSourcePath = Join-Path $root 'src\persistent_hash_bag.c'
$biMapSourcePath = Join-Path $root 'src\persistent_bi_map.c'
$multimapSourcePath = Join-Path $root 'src\persistent_hash_multimap.c'
$relationSourcePath = Join-Path $root 'src\persistent_relation.c'
$derivedGraphSourcePath = Join-Path $root 'src\persistent_directed_graph.c'
$indexedMapSourcePath = Join-Path $root 'src\persistent_indexed_map.c'
$mapPatchSourcePath = Join-Path $root 'src\persistent_map_patch.c'
$patriciaSourcePath = Join-Path $root 'src\patricia.c'
$merkleSourcePath = Join-Path $root 'src\merkle_search_tree.c'
$testSource = Join-Path $root 'tests\hamt_tests.c'
$bagTestSource = Join-Path $root 'tests\persistent_hash_bag_tests.c'
$biMapTestSource = Join-Path $root 'tests\persistent_bi_map_tests.c'
$multimapTestSource = Join-Path $root 'tests\persistent_hash_multimap_tests.c'
$derivedTestSource = Join-Path $root 'tests\persistent_derived_structures_tests.c'
$patriciaTestSource = Join-Path $root 'tests\patricia_tests.c'
$merkleTestSource = Join-Path $root 'tests\merkle_search_tree_tests.c'
$objectDir = Join-Path $buildDir 'obj'
$pdbPath = Join-Path $buildDir 'hamt_tests.pdb'
$exePath = Join-Path $buildDir 'hamt_tests.exe'
$bagPdbPath = Join-Path $buildDir 'persistent_hash_bag_tests.pdb'
$bagExePath = Join-Path $buildDir 'persistent_hash_bag_tests.exe'
$biMapPdbPath = Join-Path $buildDir 'persistent_bi_map_tests.pdb'
$biMapExePath = Join-Path $buildDir 'persistent_bi_map_tests.exe'
$multimapPdbPath = Join-Path $buildDir 'persistent_hash_multimap_tests.pdb'
$multimapExePath = Join-Path $buildDir 'persistent_hash_multimap_tests.exe'
$derivedPdbPath = Join-Path $buildDir 'persistent_derived_structures_tests.pdb'
$derivedExePath = Join-Path $buildDir 'persistent_derived_structures_tests.exe'
$patriciaPdbPath = Join-Path $buildDir 'patricia_tests.pdb'
$patriciaExePath = Join-Path $buildDir 'patricia_tests.exe'
$merklePdbPath = Join-Path $buildDir 'merkle_search_tree_tests.pdb'
$merkleExePath = Join-Path $buildDir 'merkle_search_tree_tests.exe'

New-Item -ItemType Directory -Force -Path $objectDir | Out-Null

if ($RunTests) {
    . $headlessTestHelper
    $null = Enable-HeadlessTestMode
}

& $visualCppHelper -IncludePrerelease

$commonArgs = @(
    '/nologo',
    '/TC',
    '/std:c17',
    '/EHsc-',
    '/permissive-',
    '/W4',
    '/WX',
    # <stdatomic.h> is gated behind this switch in MSVC's C11/C17 modes.
    '/experimental:c11atomics',
    '/wd4200',
    '/DD7_HAMT_TESTING',
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

& cl.exe @commonArgs @configurationArgs "/Fd$bagPdbPath" "/Fe:$bagExePath" `
    $hamtSourcePath $bagSourcePath $bagTestSource
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed for the persistent hash bag tests with exit code $LASTEXITCODE."
}

& cl.exe @commonArgs @configurationArgs "/Fd$biMapPdbPath" "/Fe:$biMapExePath" `
    $hamtSourcePath $biMapSourcePath $biMapTestSource
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed for the persistent bimap tests with exit code $LASTEXITCODE."
}

& cl.exe @commonArgs @configurationArgs "/Fd$multimapPdbPath" "/Fe:$multimapExePath" `
    $hamtSourcePath $multimapSourcePath $relationSourcePath $multimapTestSource
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed for the persistent hash multimap tests with exit code $LASTEXITCODE."
}

& cl.exe @commonArgs @configurationArgs "/Fd$derivedPdbPath" "/Fe:$derivedExePath" `
    $hamtSourcePath $multimapSourcePath $relationSourcePath $derivedGraphSourcePath `
    $indexedMapSourcePath $mapPatchSourcePath $derivedTestSource
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed for the derived persistent structure tests with exit code $LASTEXITCODE."
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

    & $bagExePath
    if ($LASTEXITCODE -ne 0) {
        throw "persistent_hash_bag_tests.exe failed with exit code $LASTEXITCODE."
    }

    & $biMapExePath
    if ($LASTEXITCODE -ne 0) {
        throw "persistent_bi_map_tests.exe failed with exit code $LASTEXITCODE."
    }

    & $multimapExePath
    if ($LASTEXITCODE -ne 0) {
        throw "persistent_hash_multimap_tests.exe failed with exit code $LASTEXITCODE."
    }

    & $derivedExePath
    if ($LASTEXITCODE -ne 0) {
        throw "persistent_derived_structures_tests.exe failed with exit code $LASTEXITCODE."
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
