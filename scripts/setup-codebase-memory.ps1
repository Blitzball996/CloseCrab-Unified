# setup-codebase-memory.ps1
# Downloads the codebase-memory MCP server binary (knowledge-graph engine) into
# ~/.crab/tools/ so CloseCrab can auto-discover and use it.
#
# CloseCrab does NOT bundle this 250MB+ binary (too large for git). This script
# fetches the official prebuilt release from DeusData/codebase-memory-mcp.
#
# Usage:   pwsh -File scripts/setup-codebase-memory.ps1
# Re-run anytime to update; it overwrites the existing binary.

$ErrorActionPreference = 'Stop'

# --- Config ---------------------------------------------------------------
$Version = 'v0.8.1'
# UI variant = includes the browser graph visualization (--ui=true, port 9749).
$Asset   = 'codebase-memory-mcp-ui-windows-amd64.zip'
$Url     = "https://github.com/DeusData/codebase-memory-mcp/releases/download/$Version/$Asset"

# Install into the user's CloseCrab home so it's shared across all projects.
$ToolsDir = Join-Path $env:USERPROFILE '.crab\tools'
$ExePath  = Join-Path $ToolsDir 'codebase-memory-mcp.exe'
$ZipPath  = Join-Path $ToolsDir 'codebase-memory-mcp.zip'
$ExtractDir = Join-Path $ToolsDir '_extract'

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  CloseCrab Knowledge Graph Setup" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# Already installed?
if (Test-Path $ExePath) {
    $sizeMB = [math]::Round((Get-Item $ExePath).Length / 1MB, 0)
    Write-Host "Already installed: $ExePath ($sizeMB MB)" -ForegroundColor Green
    Write-Host "Delete it and re-run to force re-download." -ForegroundColor DarkGray
    exit 0
}

New-Item -ItemType Directory -Force $ToolsDir | Out-Null

Write-Host "[1/3] Downloading $Asset ($Version)..." -ForegroundColor Yellow
Write-Host "      (~37MB, may take a minute)" -ForegroundColor DarkGray
$sw = [Diagnostics.Stopwatch]::StartNew()
try {
    # Invoke-WebRequest handles large GitHub release downloads reliably even on
    # slow/proxied connections (curl tended to stall here).
    Invoke-WebRequest -Uri $Url -OutFile $ZipPath -MaximumRedirection 5 -TimeoutSec 0
} catch {
    Write-Host "[ERROR] Download failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Check your network/proxy, or download manually from:" -ForegroundColor Red
    Write-Host "  $Url" -ForegroundColor Red
    Write-Host "and extract codebase-memory-mcp.exe into: $ToolsDir" -ForegroundColor Red
    exit 1
}
Write-Host "      done in $([int]$sw.Elapsed.TotalSeconds)s" -ForegroundColor DarkGray

Write-Host "[2/3] Extracting..." -ForegroundColor Yellow
if (Test-Path $ExtractDir) { Remove-Item $ExtractDir -Recurse -Force }
Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force
$found = Get-ChildItem $ExtractDir -Recurse -Filter 'codebase-memory-mcp.exe' | Select-Object -First 1
if (-not $found) {
    Write-Host "[ERROR] codebase-memory-mcp.exe not found in the archive." -ForegroundColor Red
    exit 1
}
Move-Item $found.FullName $ExePath -Force
Remove-Item $ZipPath, $ExtractDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "[3/3] Verifying..." -ForegroundColor Yellow
$ver = & $ExePath --version 2>&1 | Select-Object -First 1
Write-Host "      $ver" -ForegroundColor DarkGray

Write-Host ""
Write-Host "Installed: $ExePath" -ForegroundColor Green
Write-Host ""
Write-Host "CloseCrab will auto-detect it on next launch (knowledge-graph tools" -ForegroundColor White
Write-Host "appear as mcp__codebase-memory__*). To build a graph for a project," -ForegroundColor White
Write-Host "run scripts/build-graph.bat or just ask CloseCrab to index it." -ForegroundColor White
