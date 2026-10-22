[CmdletBinding()]
param(
    [string]$Version,
    [string]$OutputDirectory,
    [switch]$AllowDirty
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Version) { $Version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() }
$launchers = Join-Path $projectRoot 'build-release-launchers'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'releases' }
& (Join-Path $PSScriptRoot 'build-launcher.ps1') -OutputDirectory $launchers
$releaseArgs = @((Join-Path $PSScriptRoot 'release\build_release.py'), '--version', $Version,
    '--launchers', $launchers, '--output', [IO.Path]::GetFullPath($OutputDirectory))
if ($AllowDirty) { $releaseArgs += '--allow-dirty' }
if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 @releaseArgs
} else {
    & python @releaseArgs
}
if ($LASTEXITCODE -ne 0) { throw 'Public release packaging failed.' }
