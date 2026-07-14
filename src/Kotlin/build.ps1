param(
    [ValidateSet("All", "Hamt", "FingerTree", "Tungsten")]
    [string]$Workspace = "All"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "..\..\eng\Enable-HeadlessTestMode.ps1")
$null = Enable-HeadlessTestMode

$KotlinVersion = "2.4.0"
$KotlinCompilerSha256 = "ba1b9e6eb6ddc3275079224f2e9ea4a2b02eef7d59ce2d38404f04b22613c20a"
$Root = $PSScriptRoot
$BuildRoot = Join-Path $Root "build"
$ToolsRoot = Join-Path $BuildRoot "tools"
$DownloadsRoot = Join-Path $ToolsRoot "downloads"
$IsWindowsHost = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)

New-Item -ItemType Directory -Force $DownloadsRoot | Out-Null

function Invoke-Download {
    param(
        [string]$Uri,
        [string]$OutFile
    )

    if (Test-Path -LiteralPath $OutFile) {
        return
    }

    Write-Host "Downloading $Uri"
    Invoke-WebRequest -Uri $Uri -OutFile $OutFile
}

function Expand-ZipIfMissing {
    param(
        [string]$ZipFile,
        [string]$Destination,
        [string]$Marker
    )

    if (Test-Path -LiteralPath $Marker) {
        return
    }

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    New-Item -ItemType Directory -Force $Destination | Out-Null
    Expand-Archive -LiteralPath $ZipFile -DestinationPath $Destination -Force
    if (-not (Test-Path -LiteralPath $Marker)) {
        New-Item -ItemType File -Force $Marker | Out-Null
    }
}

function Get-JavaMajorVersion {
    param([string]$Java)

    $versionLine = (& $Java -version 2>&1 | Select-Object -First 1).ToString()
    if ($versionLine -match '"(?<major>\d+)(\.|")') {
        return [int]$Matches["major"]
    }

    return 0
}

function Get-JavaToolchain {
    $java = Get-Command java -ErrorAction SilentlyContinue
    if ($java -and (Get-JavaMajorVersion $java.Source) -ge 21) {
        return @{ Java = $java.Source; JavaHome = $null }
    }

    if (-not $IsWindowsHost) {
        throw "Java 21+ was not found on PATH. Install a JDK 21+ runtime or set PATH/JAVA_HOME before running this script on non-Windows hosts."
    }

    $jdkZip = Join-Path $DownloadsRoot "temurin-jdk-21-windows-x64.zip"
    $jdkRoot = Join-Path $ToolsRoot "jdk"
    Invoke-Download "https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse" $jdkZip
    Expand-ZipIfMissing $jdkZip $jdkRoot (Join-Path $jdkRoot "expanded.marker")

    $javaExe = Get-ChildItem -Path $jdkRoot -Recurse -Filter "java.exe" |
        Where-Object { $_.FullName -match "\\bin\\java.exe$" } |
        Select-Object -First 1
    if (-not $javaExe) {
        throw "Downloaded JDK did not contain bin\java.exe"
    }

    $javaHome = Split-Path (Split-Path $javaExe.FullName -Parent) -Parent
    return @{ Java = $javaExe.FullName; JavaHome = $javaHome }
}

function Get-KotlinCompiler {
    param([string]$JavaHome)

    $compilerZip = Join-Path $DownloadsRoot "kotlin-compiler-$KotlinVersion.zip"
    $compilerRoot = Join-Path $ToolsRoot "kotlin"
    $compilerCommand = if ($IsWindowsHost) { "kotlinc.bat" } else { "kotlinc" }
    $compilerBin = Join-Path (Join-Path $compilerRoot "kotlinc") "bin"
    $compilerPath = Join-Path $compilerBin $compilerCommand
    Invoke-Download "https://github.com/JetBrains/kotlin/releases/download/v$KotlinVersion/kotlin-compiler-$KotlinVersion.zip" $compilerZip

    $actualHash = (Get-FileHash -LiteralPath $compilerZip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $KotlinCompilerSha256) {
        throw "Unexpected Kotlin compiler SHA256: $actualHash"
    }

    Expand-ZipIfMissing $compilerZip $compilerRoot $compilerPath

    if (-not (Test-Path -LiteralPath $compilerPath)) {
        throw "Kotlin compiler was not found at $compilerPath"
    }

    if ($JavaHome) {
        $env:JAVA_HOME = $JavaHome
        $env:PATH = "$(Join-Path $JavaHome "bin");$env:PATH"
    }

    return $compilerPath
}

function Invoke-KotlinWorkspaceTests {
    param(
        [string]$Name,
        [string]$RelativePath,
        [string]$Kotlinc,
        [string]$Java,
        [string[]]$AdditionalSourceRoots = @()
    )

    $workspaceRoot = Join-Path $Root $RelativePath
    $sourceRoots = @(
        Join-Path $workspaceRoot "src"
        Join-Path $workspaceRoot "test"
    ) + ($AdditionalSourceRoots | ForEach-Object { Join-Path $Root $_ })
    $sources = @($sourceRoots | ForEach-Object {
        Get-ChildItem -Path $_ -Recurse -Filter "*.kt" | Sort-Object FullName | ForEach-Object FullName
    })

    if ($sources.Count -eq 0) {
        throw "No Kotlin sources found for $Name"
    }

    $outDir = Join-Path $BuildRoot $Name
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $jar = Join-Path $outDir "$($Name.ToLowerInvariant())-tests.jar"

    Write-Host "Compiling $Name Kotlin tests"
    $previousKotlinOptions = $env:KOTLIN_OPTS
    $backendArguments = @()
    if ($IsWindowsHost) {
        # kotlinc.bat preserves '=' in inherited KOTLIN_OPTS but its argument loop rewrites this
        # advanced option into the deprecated two-token form.
        $env:KOTLIN_OPTS = "$previousKotlinOptions -Xbackend-threads=1".Trim()
    } else {
        $backendArguments = @("-Xbackend-threads=1")
    }
    try {
        & $Kotlinc @sources @backendArguments "-jvm-target" "21" "-include-runtime" "-d" $jar
    } finally {
        if ($null -eq $previousKotlinOptions) {
            Remove-Item Env:KOTLIN_OPTS -ErrorAction SilentlyContinue
        } else {
            $env:KOTLIN_OPTS = $previousKotlinOptions
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Name Kotlin compilation failed"
    }

    Write-Host "Running $Name Kotlin tests"
    & $Java "-Djava.awt.headless=true" "-jar" $jar
    if ($LASTEXITCODE -ne 0) {
        throw "$Name Kotlin tests failed"
    }
}

$javaToolchain = Get-JavaToolchain
$kotlinc = Get-KotlinCompiler $javaToolchain.JavaHome

if ($Workspace -eq "All" -or $Workspace -eq "Hamt") {
    Invoke-KotlinWorkspaceTests "Hamt" "Hamt" $kotlinc $javaToolchain.Java
}

if ($Workspace -eq "All" -or $Workspace -eq "FingerTree") {
    Invoke-KotlinWorkspaceTests "FingerTree" "FingerTree" $kotlinc $javaToolchain.Java
}

if ($Workspace -eq "All" -or $Workspace -eq "Tungsten") {
    Invoke-KotlinWorkspaceTests "Tungsten" "Tungsten" $kotlinc $javaToolchain.Java @("Hamt/src")
}
