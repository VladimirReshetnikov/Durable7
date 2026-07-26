<#
.SYNOPSIS
Configures, builds, and optionally tests the C++ workspaces.

.DESCRIPTION
Imports a Visual C++ environment, then drives CMake and CTest. The tool paths are parameters rather
than assumptions, so a machine with a different Visual Studio layout can be pointed at its own.

.PARAMETER Workspace
Which workspaces to build, or All for every one.

.PARAMETER Configuration
The build configuration.

.PARAMETER RunTests
Run the test suites after building.

.PARAMETER VisualStudioDevCmd
Path to VsDevCmd.bat, used to import the C++ build environment.

.PARAMETER CMake
Path to cmake.exe.

.PARAMETER CTest
Path to ctest.exe.
#>

[CmdletBinding()]
param(
    [ValidateSet('All', 'Hamt', 'FingerTree', 'Ordered')]
    [string[]] $Workspace = @('All'),

    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [switch] $RunTests,

    [string] $VisualStudioDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat',

    [string] $CMake = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',

    [string] $CTest = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($RunTests) {
    . (Join-Path $PSScriptRoot '..\..\eng\Enable-HeadlessTestMode.ps1')
    $null = Enable-HeadlessTestMode
}

function Invoke-HamtBuild {
    Push-Location -LiteralPath (Join-Path $PSScriptRoot 'Hamt')
    try {
        & .\build.ps1 -Configuration $Configuration -RunTests:$RunTests
        if ($LASTEXITCODE -ne 0) {
            throw "C++ HAMT build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-FingerTreeBuild {
    $preset = if ($Configuration -eq 'Release') { 'msvc-release' } else { 'msvc-debug' }
    $steps = @(
        "call `"$VisualStudioDevCmd`" -arch=x64 -host_arch=x64",
        "`"$CMake`" --preset $preset",
        "`"$CMake`" --build --preset $preset --parallel 1"
    )

    if ($RunTests) {
        $steps += "`"$CTest`" --preset $preset --parallel 1 --output-on-failure"
    }

    Push-Location -LiteralPath (Join-Path $PSScriptRoot 'FingerTree')
    try {
        & cmd.exe /d /c ($steps -join ' && ')
        if ($LASTEXITCODE -ne 0) {
            throw "C++ FingerTree build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-OrderedBuild {
    $preset = if ($Configuration -eq 'Release') { 'msvc-release' } else { 'msvc-debug' }
    $steps = @(
        "call `"$VisualStudioDevCmd`" -arch=x64 -host_arch=x64",
        "`"$CMake`" --preset $preset",
        "`"$CMake`" --build --preset $preset --parallel 1"
    )

    if ($RunTests) {
        $steps += "`"$CTest`" --preset $preset --parallel 1 --output-on-failure"
    }

    Push-Location -LiteralPath (Join-Path $PSScriptRoot 'Ordered')
    try {
        & cmd.exe /d /c ($steps -join ' && ')
        if ($LASTEXITCODE -ne 0) {
            throw "C++ Ordered build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

$selected = if ($Workspace -contains 'All') { @('Hamt', 'FingerTree', 'Ordered') } else { $Workspace }

foreach ($item in $selected) {
    switch ($item) {
        'Hamt' { Invoke-HamtBuild }
        'FingerTree' { Invoke-FingerTreeBuild }
        'Ordered' { Invoke-OrderedBuild }
    }
}
