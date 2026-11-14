# flash.ps1 - flash the prebuilt merged firmware (ASCII only: PS 5.1 mangles UTF-8 without BOM)
#
# Usage:
#   .\flash.ps1                 # asks for the COM port
#   .\flash.ps1 -Port COM6
param(
    [string]$Port = ""
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin = Join-Path $here 'firmware\attitude-indicator-merged.bin'

if (-not (Test-Path $bin)) { throw "Firmware not found: $bin" }

if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Host 'Available COM ports:'
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports) { $ports | ForEach-Object { Write-Host "  $_" } } else { Write-Host '  (none)' }
    $Port = Read-Host 'Enter port (e.g. COM6)'
}
if ([string]::IsNullOrWhiteSpace($Port)) { throw 'No port given' }

# Locate an esptool: prefer the one bundled with the ESP32 Arduino core, then PATH.
$esptool = $null
$corePath = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py'
if (Test-Path $corePath) {
    $found = Get-ChildItem $corePath -Recurse -Filter 'esptool.exe' -ErrorAction SilentlyContinue |
             Sort-Object FullName -Descending | Select-Object -First 1
    if ($found) { $esptool = $found.FullName }
}
if (-not $esptool) {
    foreach ($cand in @('esptool.py', 'esptool')) {
        $c = Get-Command $cand -ErrorAction SilentlyContinue
        if ($c) { $esptool = $c.Source; break }
    }
}
if (-not $esptool) {
    Write-Host ''
    Write-Host 'esptool not found. Install it with:' -ForegroundColor Yellow
    Write-Host '    pip install esptool' -ForegroundColor Yellow
    throw 'esptool missing'
}

Write-Host ''
Write-Host "Firmware : $bin"
Write-Host "Port     : $Port"
Write-Host "esptool  : $esptool"
Write-Host ''
Write-Host 'If flashing fails, hold BOOT, tap RESET, release BOOT and retry.' -ForegroundColor Cyan
Write-Host ''

& $esptool --chip esp32s3 -p $Port -b 921600 write_flash 0x0 $bin
if ($LASTEXITCODE -ne 0) { throw "Flashing failed (exit code $LASTEXITCODE)" }

Write-Host ''
Write-Host 'Done. The board reboots into the attitude indicator.' -ForegroundColor Green
Write-Host 'Tip: place it in the attitude you consider level, then short press BOOT to cage.' -ForegroundColor Green
