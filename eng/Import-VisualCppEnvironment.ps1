<#
.SYNOPSIS
Imports a Visual C++ (MSVC) build environment (vcvars) into the current PowerShell process.

.DESCRIPTION
Standalone MSVC environment bootstrap for PowerShell: locates the newest Visual Studio
installation carrying the requested VC++ toolset component via vswhere.exe, runs the
matching vcvars batch file in a cmd.exe child, and copies every resulting environment
variable (PATH, INCLUDE, LIB, VCToolsVersion, ...) into this PowerShell process, so that
cl.exe, link.exe, rc.exe, msbuild-free raw compiles, etc. work directly from PowerShell.

vswhere.exe is looked up on PATH first, then at the canonical installer location
"${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe".

Because Set-Item Env: mutations are process-wide, the script works both ways:

    # One-shot import into the current session (call operator or direct invocation):
    & eng\Import-VisualCppEnvironment.ps1 -Architecture x64

    # Dot-source to get the Import-VisualCppEnvironment function without importing yet:
    . eng\Import-VisualCppEnvironment.ps1
    Import-VisualCppEnvironment -Architecture x64_arm64 -IncludePrerelease

The import is idempotent: when both INCLUDE and LIB are already set, the import returns
immediately without spawning cmd.exe. Use -Force to re-import anyway (e.g. to switch
target architecture within one session).

.PARAMETER Architecture
Host/target architecture of the toolset environment to import. One of:
  x64        native x64 host and target        (vcvars64.bat, the default)
  x86        native x86 host and target        (vcvars32.bat)
  arm64      native ARM64 host and target      (vcvarsarm64.bat)
  x64_x86    x64 host cross-targeting x86      (vcvarsamd64_x86.bat)
  x64_arm64  x64 host cross-targeting ARM64    (vcvarsamd64_arm64.bat)

.PARAMETER RequiredComponent
Visual Studio workload/component ID that vswhere requires of an installation. Default:
'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' (the native x86/x64 VC++ toolset).
For ARM64 targets you may want 'Microsoft.VisualStudio.Component.VC.Tools.ARM64'.

.PARAMETER FallbackVcvarsPath
Explicit path to a vcvars batch file to run when vswhere is missing or reports no
suitable installation. For example, a version-pinned preview install such as
'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat'.
No fallback is tried unless this parameter is supplied; prefer -IncludePrerelease for
preview installs.

.PARAMETER IncludePrerelease
Also consider prerelease/preview Visual Studio installations (passes -prerelease to
vswhere). Required on machines whose only VC++ toolset lives in a Preview/Insiders VS.

.PARAMETER Force
Re-import even if INCLUDE and LIB are already set in the current process.

.EXAMPLE
& eng\Import-VisualCppEnvironment.ps1 -IncludePrerelease
Get-Command cl.exe   # now resolves

.EXAMPLE
. eng\Import-VisualCppEnvironment.ps1
Import-VisualCppEnvironment -Architecture x64_arm64 -IncludePrerelease -Force

.NOTES
Repository-owned build tooling. `src/C/Hamt/build.ps1` and `src/Cpp/Hamt/build.ps1`
call this to put cl.exe and link.exe on PATH before compiling, so those gates run from
a plain PowerShell process without a developer command prompt.
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'arm64', 'x64_x86', 'x64_arm64')]
    [string] $Architecture = 'x64',

    [string] $RequiredComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',

    [string] $FallbackVcvarsPath,

    [switch] $IncludePrerelease,

    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Import-BatchEnvironment {
    <#
    .SYNOPSIS
    Runs a batch file in cmd.exe and copies the resulting environment into this process.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $BatPath
    )

    cmd.exe /c "`"$BatPath`" >nul && set" | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') {
            Set-Item -Path "Env:\$($matches[1])" -Value $matches[2]
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Environment batch script '$BatPath' failed with exit code $LASTEXITCODE; no environment was imported."
    }
}

function Import-VisualCppEnvironment {
    <#
    .SYNOPSIS
    Locates a VC++ toolset via vswhere and imports its vcvars environment into this process.
    #>
    [CmdletBinding()]
    param(
        [ValidateSet('x64', 'x86', 'arm64', 'x64_x86', 'x64_arm64')]
        [string] $Architecture = 'x64',

        [string] $RequiredComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',

        [string] $FallbackVcvarsPath,

        [switch] $IncludePrerelease,

        [switch] $Force
    )

    if (-not $Force -and $env:INCLUDE -and $env:LIB) {
        Write-Verbose 'INCLUDE and LIB are already set; skipping import (use -Force to re-import).'
        return
    }

    $vcvarsFile = @{
        'x64'       = 'vcvars64.bat'
        'x86'       = 'vcvars32.bat'
        'arm64'     = 'vcvarsarm64.bat'
        'x64_x86'   = 'vcvarsamd64_x86.bat'
        'x64_arm64' = 'vcvarsamd64_arm64.bat'
    }[$Architecture]

    # Locate vswhere: PATH first, then the canonical VS Installer location.
    $installerVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vswhere = $null
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhereCommand) {
        $vswhere = $vswhereCommand.Source
    }
    elseif (Test-Path $installerVswhere) {
        $vswhere = $installerVswhere
    }

    $installPath = $null
    if ($vswhere) {
        $vswhereArgs = @('-latest', '-products', '*', '-requires', $RequiredComponent, '-property', 'installationPath')
        if ($IncludePrerelease) {
            $vswhereArgs = @('-prerelease') + $vswhereArgs
        }
        $installPath = & $vswhere @vswhereArgs | Select-Object -First 1
        if ($installPath) {
            $vcvars = Join-Path $installPath "VC\Auxiliary\Build\$vcvarsFile"
            if (Test-Path $vcvars) {
                Write-Verbose "Importing MSVC environment from '$vcvars'."
                Import-BatchEnvironment -BatPath $vcvars
                return
            }
        }
    }

    if ($FallbackVcvarsPath -and (Test-Path $FallbackVcvarsPath)) {
        Write-Verbose "Importing MSVC environment from fallback '$FallbackVcvarsPath'."
        Import-BatchEnvironment -BatPath $FallbackVcvarsPath
        return
    }

    $reasons = @()
    if (-not $vswhere) {
        $reasons += "vswhere.exe was found neither on PATH nor at '$installerVswhere'"
    }
    elseif (-not $installPath) {
        $hint = if ($IncludePrerelease) { '' } else { ' (preview/Insiders installs are excluded by default; try -IncludePrerelease)' }
        $reasons += "vswhere ('$vswhere') reported no Visual Studio installation with component '$RequiredComponent'$hint"
    }
    else {
        $reasons += "the Visual Studio installation at '$installPath' has no '$vcvarsFile' under VC\Auxiliary\Build"
    }
    if ($FallbackVcvarsPath) {
        $reasons += "the fallback vcvars path '$FallbackVcvarsPath' does not exist"
    }
    else {
        $reasons += 'no -FallbackVcvarsPath was supplied'
    }
    throw "Could not find a Visual C++ '$vcvarsFile' environment script: $($reasons -join '; ')."
}

# Direct invocation (& path\Import-VisualCppEnvironment.ps1 [args]) performs the import
# immediately; dot-sourcing (. path\...) only defines the functions.
if ($MyInvocation.InvocationName -ne '.') {
    Import-VisualCppEnvironment `
        -Architecture $Architecture `
        -RequiredComponent $RequiredComponent `
        -FallbackVcvarsPath $FallbackVcvarsPath `
        -IncludePrerelease:$IncludePrerelease `
        -Force:$Force
}
