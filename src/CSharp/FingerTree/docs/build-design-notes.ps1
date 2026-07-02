#!/usr/bin/env pwsh
# Rebuilds FingerTree-Design-Notes.pdf from the .tex source. Thin wrapper around the shared
# Scriptorium Build-LatexDoc tool (two lualatex passes + byproduct cleanup + MiKTeX PATH
# fallback — see C:\Scriptorium\TOOLS.md). Run from anywhere:
#
#     pwsh -File C:\DataStructures\src\CSharp\FingerTree\docs\build-design-notes.ps1

[CmdletBinding()]
param(
    [switch]$KeepAux
)

$ErrorActionPreference = 'Stop'

$scriptoriumRoot =
    @($env:SCRIPTORIUM, (Join-Path $PSScriptRoot '..\..\..\Scriptorium'), 'C:\Scriptorium') |
    Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
    Select-Object -First 1
if (-not $scriptoriumRoot) {
    throw 'Scriptorium toolbox not found (set $env:SCRIPTORIUM or clone it beside this repository).'
}

& (Join-Path $scriptoriumRoot 'render\Build-LatexDoc.ps1') `
    (Join-Path $PSScriptRoot 'FingerTree-Design-Notes.tex') `
    -KeepAux:$KeepAux
