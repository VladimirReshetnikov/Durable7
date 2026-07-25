#!/usr/bin/env pwsh
# Rebuilds FingerTree-Design-Notes.pdf from the .tex source. Thin wrapper around the
# repository's eng\Build-LatexDoc.ps1 (two lualatex passes + byproduct cleanup + MiKTeX
# PATH fallback). Run from anywhere:
#
#     pwsh -File <repo>\src\CSharp\docs\FingerTree\build-design-notes.ps1

[CmdletBinding()]
param(
    [switch]$KeepAux
)

$ErrorActionPreference = 'Stop'

& (Join-Path $PSScriptRoot '..\..\..\..\eng\Build-LatexDoc.ps1') `
    (Join-Path $PSScriptRoot 'FingerTree-Design-Notes.tex') `
    -KeepAux:$KeepAux
