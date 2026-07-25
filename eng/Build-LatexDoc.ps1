#!/usr/bin/env pwsh
# Build-LatexDoc.ps1 — generic N-pass LaTeX builder.
#
# Repository-owned build tooling. Takes the .tex path, engine, pass count, and cleanup
# list as parameters; src/CSharp/docs/FingerTree/build-design-notes.ps1 wraps it to
# rebuild the FingerTree design notes.
#
# Usage:
#     pwsh eng\Build-LatexDoc.ps1 path\to\doc.tex
#     pwsh eng\Build-LatexDoc.ps1 doc.tex -Engine xelatex -Passes 3
#     pwsh eng\Build-LatexDoc.ps1 doc.tex -KeepAux
#
# Runs the engine the requested number of passes (default 2, so the table of contents
# and cross-references resolve), warns if the log still reports undefined references,
# then removes the LaTeX byproducts (default .aux/.log/.out/.toc) unless -KeepAux is
# given. Requires MiKTeX or another TeX distribution providing the chosen engine
# (MiKTeX package auto-install handles any missing LaTeX packages on first run).

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$TexPath,

    [ValidateRange(1, 10)]
    [int]$Passes = 2,

    [ValidateSet('lualatex', 'xelatex', 'pdflatex')]
    [string]$Engine = 'lualatex',

    [string[]]$CleanExtensions = @('.aux', '.log', '.out', '.toc'),

    [switch]$KeepAux
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Do not let the engine's stderr/exit-code interact with the stop preference; we check $LASTEXITCODE ourselves.
$PSNativeCommandUseErrorActionPreference = $false

$tex = (Resolve-Path -LiteralPath $TexPath).Path
$docsDir = Split-Path -Parent $tex
$name = [System.IO.Path]::GetFileNameWithoutExtension($tex)

# Ensure the engine is reachable; fall back to the MiKTeX default install location on this machine.
if (-not (Get-Command $Engine -ErrorAction SilentlyContinue)) {
    $miktexBin = Join-Path $env:LOCALAPPDATA 'Programs\MiKTeX\miktex\bin\x64'
    if (Test-Path (Join-Path $miktexBin "$Engine.exe")) { $env:PATH = "$miktexBin;$env:PATH" }
}
if (-not (Get-Command $Engine -ErrorAction SilentlyContinue)) {
    throw "$Engine not found. Install MiKTeX (winget install MiKTeX.MiKTeX) or add its bin directory to PATH."
}

Push-Location $docsDir
try {
    foreach ($pass in 1..$Passes) {
        Write-Host "$Engine pass $pass of $Passes ..."
        & $Engine -interaction=nonstopmode -halt-on-error "$name.tex" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "$Engine failed on pass $pass; see $name.log for the error." }
    }

    $log = Get-Content "$name.log" -Raw
    $undefined = @($log -split "`n" | Where-Object { $_ -match 'undefined' }).Count
    if ($undefined -gt 0) { Write-Warning "$undefined undefined reference/citation warning(s); inspect $name.log." }

    if (-not $KeepAux) {
        foreach ($ext in $CleanExtensions) {
            $byproduct = "$name$ext"
            if (Test-Path $byproduct) { Remove-Item -Force $byproduct }
        }
    }

    $pdf = Join-Path $docsDir "$name.pdf"
    $kb = [math]::Round((Get-Item $pdf).Length / 1KB)
    Write-Host "Built $pdf ($kb KB)."
}
finally {
    Pop-Location
}
