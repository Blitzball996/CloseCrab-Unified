# setup-ripgrep.ps1
# Downloads the ripgrep (rg.exe) binary into third_party/ripgrep/ so the build /
# installer can bundle it. The Grep tool prefers this fast, crash-isolated
# external binary over its built-in in-process search (see GrepTool::rgCommand()).
#
# CloseCrab does NOT commit rg.exe to git (binaries don't belong in the repo —
# same convention as setup-codebase-memory.ps1). This script fetches the official
# prebuilt release from BurntSushi/ripgrep.
#
# Usage:   pwsh -File scripts/setup-ripgrep.ps1
# Re-run anytime to update; it overwrites the existing binary.

$ErrorActionPreference = 'Stop'

# --- Config ---------------------------------------------------------------
$Version = '14.1.1'
$Asset   = "ripgrep-$Version-x86_64-pc-windows-msvc.zip"
$Url     = "https://github.com/BurntSushi/ripgrep/releases/download/$Version/$Asset"

# Land it in the source tree where installer.iss bundles from
# (Source: "third_party\ripgrep\rg.exe"; DestDir: "{app}\tools").
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot   = Split-Path -Parent $ScriptDir
$ToolsDir   = Join-Path $RepoRoot 'third_party\ripgrep'
$ExePath    = Join-Path $ToolsDir 'rg.exe'
$ZipPath    = Join-Path $ToolsDir 'ripgrep.zip'
$ExtractDir = Join-Path $ToolsDir '_extract'

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  CloseCrab ripgrep (Grep tool) Setup" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# Already installed?
if (Test-Path $ExePath) {
    $sizeMB = [math]::Round((Get-Item $ExePath).Length / 1MB, 1)
    Write-Host "Already present: $ExePath ($sizeMB MB)" -ForegroundColor Green
    Write-Host "Delete it and re-run to force re-download." -ForegroundColor DarkGray
    exit 0
}

New-Item -ItemType Directory -Force $ToolsDir | Out-Null

Write-Host "[1/3] Downloading $Asset..." -ForegroundColor Yellow
Write-Host "      (~2MB)" -ForegroundColor DarkGray
# Honor a local proxy (e.g. clash on 7897) if HTTPS_PROXY is set; otherwise direct.
$proxy = $env:HTTPS_PROXY; if (-not $proxy) { $proxy = $env:https_proxy }
try {
    if ($proxy) {
        Invoke-WebRequest -Uri $Url -OutFile $ZipPath -Proxy $proxy -MaximumRedirection 5 -TimeoutSec 120
    } else {
        Invoke-WebRequest -Uri $Url -OutFile $ZipPath -MaximumRedirection 5 -TimeoutSec 120
    }
} catch {
    Write-Host "[ERROR] Download failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Set HTTPS_PROXY if you need a proxy, or download manually from:" -ForegroundColor Red
    Write-Host "  $Url" -ForegroundColor Red
    Write-Host "and extract rg.exe into: $ToolsDir" -ForegroundColor Red
    exit 1
}

Write-Host "[2/3] Extracting..." -ForegroundColor Yellow
if (Test-Path $ExtractDir) { Remove-Item $ExtractDir -Recurse -Force }
Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force
$found = Get-ChildItem $ExtractDir -Recurse -Filter 'rg.exe' | Select-Object -First 1
if (-not $found) {
    Write-Host "[ERROR] rg.exe not found in the archive." -ForegroundColor Red
    exit 1
}
Move-Item $found.FullName $ExePath -Force
Remove-Item $ZipPath, $ExtractDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "[3/3] Verifying..." -ForegroundColor Yellow
$ver = & $ExePath --version 2>&1 | Select-Object -First 1
Write-Host "      $ver" -ForegroundColor DarkGray

Write-Host ""
Write-Host "Installed: $ExePath" -ForegroundColor Green
Write-Host "The installer bundles this into {app}\tools\rg.exe; for local dev copy" -ForegroundColor White
Write-Host "it next to closecrab.exe (build\Release\tools\rg.exe)." -ForegroundColor White
