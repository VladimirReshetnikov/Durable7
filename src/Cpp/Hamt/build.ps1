<#
.SYNOPSIS
Compiles and optionally tests the C++ Hamt workspace.

.DESCRIPTION
Compiles each translation unit directly rather than through CMake, so this workspace can be built
without a generator present.

.PARAMETER Configuration
The build configuration.

.PARAMETER RunTests
Run the test executables after building.
#>

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
$packageRoot = Join-Path $buildDir 'package'
$packageIncludeDir = Join-Path $packageRoot 'include'
$testPrograms = @(
    @{
        Name = 'persistent_hamt_tests'
        Source = Join-Path $root 'tests\persistent_hamt_tests.cpp'
        Include = $includeDir
        UseTestSupport = $true
    },
    @{
        Name = 'merkle_search_tree_tests'
        Source = Join-Path $root 'tests\merkle_search_tree_tests.cpp'
        Include = $includeDir
        UseTestSupport = $true
    },
    @{
        Name = 'merkle_header_consumer'
        Source = Join-Path $root 'tests\merkle_header_consumer.cpp'
        Include = $packageIncludeDir
        UseTestSupport = $false
    }
)

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$resolvedBuildDir = [IO.Path]::GetFullPath($buildDir).TrimEnd('\') + '\'
$resolvedPackageRoot = [IO.Path]::GetFullPath($packageRoot)
if (-not $resolvedPackageRoot.StartsWith($resolvedBuildDir, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace package staging outside the configuration build directory."
}
if (Test-Path -LiteralPath $resolvedPackageRoot) {
    Remove-Item -Recurse -Force -LiteralPath $resolvedPackageRoot
}
New-Item -ItemType Directory -Force -Path $packageIncludeDir | Out-Null
Copy-Item -Recurse -Force -Path (Join-Path $includeDir 'durable7') -Destination $packageIncludeDir

if ($RunTests) {
    . $headlessTestHelper
    $null = Enable-HeadlessTestMode
}

& $visualCppHelper -IncludePrerelease

$commonArgs = @(
    '/nologo',
    '/std:c++20',
    '/EHsc',
    '/permissive-',
    '/W4',
    '/WX',
    '/Zc:__cplusplus',
    # The header-only tests instantiate enough templates in a single translation unit to exceed
    # the default COFF section count; /bigobj lifts that limit and has no runtime effect.
    '/bigobj'
)

if ($Configuration -eq 'Debug') {
    $configurationArgs = @('/Od', '/Zi', '/MDd')
}
else {
    $configurationArgs = @('/O2', '/MD', '/DNDEBUG')
}

foreach ($program in $testPrograms) {
    $objectPath = Join-Path $buildDir ($program.Name + '.obj')
    $pdbPath = Join-Path $buildDir ($program.Name + '.pdb')
    $exePath = Join-Path $buildDir ($program.Name + '.exe')
    $programArgs = @(
        "/I$($program.Include)",
        "/Fo$objectPath",
        "/Fd$pdbPath"
    )
    if ($program.UseTestSupport) {
        $programArgs += "/I$testSupportIncludeDir"
    }

    & cl.exe @commonArgs @configurationArgs @programArgs $program.Source bcrypt.lib "/Fe:$exePath"
    if ($LASTEXITCODE -ne 0) {
        throw "cl.exe failed for $($program.Name) with exit code $LASTEXITCODE."
    }
}

if ($RunTests) {
    foreach ($program in $testPrograms) {
        $exePath = Join-Path $buildDir ($program.Name + '.exe')
        & $exePath
        if ($LASTEXITCODE -ne 0) {
            throw "$($program.Name).exe failed with exit code $LASTEXITCODE."
        }
    }
}
