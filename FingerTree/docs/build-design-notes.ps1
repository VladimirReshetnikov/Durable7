#!/usr/bin/env pwsh
# Rebuilds FingerTree-Design-Notes.pdf from the .tex source with lualatex, running two passes so the table of
# contents and cross-references resolve, then removing the LaTeX byproducts (.aux/.log/.out/.toc). Run from
# anywhere:
#
#     pwsh -File src/DataStructures/FingerTree/docs/build-design-notes.ps1
#
# Requires MiKTeX (package auto-install handles any missing LaTeX packages on first run).

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
# Do not let lualatex's stderr/exit-code interact with the stop preference; we check $LASTEXITCODE ourselves.
$PSNativeCommandUseErrorActionPreference = $false

$docsDir = $PSScriptRoot
$name = 'FingerTree-Design-Notes'
$tex = Join-Path $docsDir "$name.tex"
if (-not (Test-Path $tex)) { throw "Source not found: $tex" }

# Ensure lualatex is reachable; fall back to the MiKTeX default install location on this machine.
if (-not (Get-Command lualatex -ErrorAction SilentlyContinue)) {
    $miktexBin = Join-Path $env:LOCALAPPDATA 'Programs\MiKTeX\miktex\bin\x64'
    if (Test-Path (Join-Path $miktexBin 'lualatex.exe')) { $env:PATH = "$miktexBin;$env:PATH" }
}
if (-not (Get-Command lualatex -ErrorAction SilentlyContinue)) {
    throw 'lualatex not found. Install MiKTeX (winget install MiKTeX.MiKTeX) or add its bin directory to PATH.'
}

Push-Location $docsDir
try {
    foreach ($pass in 1, 2) {
        Write-Host "lualatex pass $pass of 2 ..."
        lualatex -interaction=nonstopmode -halt-on-error "$name.tex" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "lualatex failed on pass $pass; see $name.log for the error." }
    }

    $log = Get-Content "$name.log" -Raw
    $undefined = @($log -split "`n" | Where-Object { $_ -match 'undefined' }).Count
    if ($undefined -gt 0) { Write-Warning "$undefined undefined reference/citation warning(s); inspect $name.log." }

    foreach ($ext in '.aux', '.log', '.out', '.toc') {
        $byproduct = "$name$ext"
        if (Test-Path $byproduct) { Remove-Item -Force $byproduct }
    }

    $pdf = Join-Path $docsDir "$name.pdf"
    $kb = [math]::Round((Get-Item $pdf).Length / 1KB)
    Write-Host "Built $pdf ($kb KB)."
}
finally {
    Pop-Location
}
