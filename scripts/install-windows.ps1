# =============================================================================
# CloseCrab — one-line Windows PowerShell installer
#
#   irm https://raw.githubusercontent.com/Blitzball996/CloseCrab-Unified/main/scripts/install-windows.ps1 | iex
#
# Downloads the latest Windows x64 ZIP, extracts to %LOCALAPPDATA%\CloseCrab,
# and adds it to the current user's PATH. No admin required.
# =============================================================================
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Repo   = "Blitzball996/CloseCrab-Unified"
$Asset  = "CloseCrab-Windows-x64.zip"
$ApiUrl = "https://api.github.com/repos/$Repo/releases/latest"
$InstallDir = Join-Path $env:LOCALAPPDATA "CloseCrab"

Write-Host "==> Fetching latest release info ..."
try {
    $release = Invoke-RestMethod -Uri $ApiUrl -Headers @{ "User-Agent" = "closecrab-installer" }
    $download = ($release.assets | Where-Object { $_.name -eq $Asset })[0].browser_download_url
} catch {
    # Fallback: construct URL directly (no API quota needed)
    $download = "https://github.com/$Repo/releases/latest/download/$Asset"
}

$tmp = Join-Path $env:TEMP "closecrab-install-$([System.IO.Path]::GetRandomFileName())"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$zipPath = Join-Path $tmp $Asset
Write-Host "==> Downloading $Asset ..."
Invoke-WebRequest -Uri $download -OutFile $zipPath -UseBasicParsing

Write-Host "==> Extracting ..."
Expand-Archive -Path $zipPath -DestinationPath $tmp -Force

# Archive layout: CloseCrab\closecrab.exe (mirrors the Linux tar.gz layout)
$exeSrc = Join-Path $tmp "CloseCrab\closecrab.exe"
if (-not (Test-Path $exeSrc)) {
    Write-Error "closecrab.exe not found in archive — layout may have changed."
}

Write-Host "==> Installing to $InstallDir ..."
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
# Copy exe + all bundled DLLs
Copy-Item (Join-Path $tmp "CloseCrab\*") -Destination $InstallDir -Force

# Add to user PATH (permanent, no admin, survives reboot)
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$InstallDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$InstallDir", "User")
    Write-Host "    Added $InstallDir to PATH"
}
# Also extend the current session's PATH so the user can run it immediately
$env:PATH = "$env:PATH;$InstallDir"

# Cleanup
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "============================================================"
Write-Host " CloseCrab installed!  ->  $InstallDir\closecrab.exe"
Write-Host ""
Write-Host " Run it with:           closecrab"
Write-Host " Switch model:          closecrab --set-model claude-sonnet-5"
Write-Host " (config auto-creates at %USERPROFILE%\.crab\config.yaml)"
Write-Host ""
Write-Host " PATH updated — you can run 'closecrab' right now in this"
Write-Host " terminal, or open a new one if using a different shell."
Write-Host "============================================================"
