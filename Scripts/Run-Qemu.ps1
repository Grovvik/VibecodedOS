param(
    [string]$OvmfPath = "C:\Program Files\qemu\OVMF.fd",
    [string]$BuildConfig = "Debug"
)

$ErrorActionPreference = "Stop"

$solutionDir = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $solutionDir "bin\$BuildConfig"
$espDir = Join-Path $solutionDir "ESP"
$efiOsDir = Join-Path $espDir "EFI\OS"

Write-Host "=== MicroNT QEMU Launch Script ===" -ForegroundColor Cyan

if (-not (Test-Path $OvmfPath)) {
    Write-Host "ERROR: OVMF.fd not found at $OvmfPath" -ForegroundColor Red
    Write-Host "Download from: https://github.com/tianocore/edk2/releases" -ForegroundColor Yellow
    Write-Host "Or install via: choco install ovmf" -ForegroundColor Yellow
    exit 1
}

$kernelExe = Join-Path $binDir "Kernel.exe"
$bootEfi = Join-Path $binDir "Boot.efi"

if (-not (Test-Path $kernelExe)) {
    Write-Host "ERROR: Kernel.exe not found. Build the solution first." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $bootEfi)) {
    Write-Host "ERROR: Boot.efi not found. Build the solution first." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $efiOsDir | Out-Null
$bootDir = Join-Path $espDir "EFI\Boot"
New-Item -ItemType Directory -Force -Path $bootDir | Out-Null

Copy-Item $bootEfi (Join-Path $bootDir "BootX64.efi") -Force
Copy-Item $kernelExe (Join-Path $efiOsDir "Kernel.exe") -Force

Write-Host "ESP layout:" -ForegroundColor Green
Get-ChildItem $espDir -Recurse | ForEach-Object { Write-Host "  $($_.FullName.Replace($espDir, ''))" }

$qemuArgs = @(
    "-m", "256"
    "-cpu", "qemu64"
    "-smp", "1"
    "-bios", $OvmfPath
    "-drive", "format=raw,file=fat:rw:$espDir"
    "-serial", "stdio"
    "-debugcon", "stdio"
    "-no-reboot"
    "-no-shutdown"
    "-net", "none"
    "-monitor", "none"
)

Write-Host ""
Write-Host "Launching QEMU..." -ForegroundColor Green
Write-Host "  Serial + Debugcon -> stdio" -ForegroundColor Yellow
Write-Host "  Press Ctrl+A, X to quit QEMU serial" -ForegroundColor Yellow
Write-Host ""

& qemu-system-x86_64 @qemuArgs
