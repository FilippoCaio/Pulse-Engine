param(
    [Parameter(Mandatory = $true)]
    [string]$BaseUrl,
    [string]$Executable = "build\impulso_live.exe",
    [string]$OutputDirectory = "releases",
    [string]$Notes = "New Impulso Engine release"
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$versionHeader = Join-Path $scriptRoot "src\engine_version.h"
$header = Get-Content -LiteralPath $versionHeader -Raw
$match = [regex]::Match($header, '#define\s+IMPULSO_ENGINE_VERSION\s+"([^"]+)"')
if (-not $match.Success) { throw "IMPULSO_ENGINE_VERSION was not found in $versionHeader" }
$version = $match.Groups[1].Value

$source = Join-Path $scriptRoot $Executable
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Editor executable not found: $source" }
$destinationRoot = Join-Path $scriptRoot $OutputDirectory
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
$fileName = "impulso-$version.exe"
$destination = Join-Path $destinationRoot $fileName
Copy-Item -LiteralPath $source -Destination $destination -Force
$sha = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
$downloadUrl = $BaseUrl.TrimEnd('/') + '/' + $fileName

$manifest = [ordered]@{
    version = $version
    url = $downloadUrl
    sha256 = $sha
    notes = $Notes
} | ConvertTo-Json
$manifestPath = Join-Path $destinationRoot "update-manifest.json"
[System.IO.File]::WriteAllText($manifestPath, $manifest, [System.Text.UTF8Encoding]::new($false))

Write-Host "Published local update package:"
Write-Host "  Version:  $version"
Write-Host "  Binary:   $destination"
Write-Host "  Manifest: $manifestPath"
Write-Host "Upload both files to $BaseUrl and point update.cfg to the manifest URL."
