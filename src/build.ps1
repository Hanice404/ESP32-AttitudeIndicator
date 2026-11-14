# build.ps1 -- Compile / flash AttitudeBall over USB
#
# Usage:
#   .\build.ps1                     compile only
#   .\build.ps1 -Upload             compile + flash to auto-detected COM port
#   .\build.ps1 -Upload -Port COM5  specify COM port
#   .\build.ps1 -Clean              remove build dir, then compile
#   .\build.ps1 -Monitor            open serial monitor (Ctrl+C to quit)
param(
  [switch]$Upload,
  [switch]$Clean,
  [switch]$Monitor,
  [string]$Port = ""
)

# Native tools write progress to stderr; with Stop that would abort the script.
$ErrorActionPreference = 'Continue'

$root     = 'E:\DeepSeekRuntime\APP\esp32s3-lcd185'
$exe      = Join-Path $root '_tools\arduino-cli\arduino-cli.exe'
$sketch   = Join-Path $root 'AttitudeBall'
$libs     = Join-Path $root '_libs'
$buildDir = Join-Path $root '_build'

# Whole-board definition + 3MB APP partition + CDC serial (so Serial goes over USB-C)
$fqbn = 'esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600'

if (-not (Test-Path $exe)) { Write-Host "arduino-cli not found: $exe" -ForegroundColor Red; exit 1 }

if ($Clean -and (Test-Path $buildDir)) {
  Write-Host "Cleaning build dir ..." -ForegroundColor Yellow
  Remove-Item $buildDir -Recurse -Force
}

Write-Host "=== Compiling ===" -ForegroundColor Cyan
$sw = [System.Diagnostics.Stopwatch]::StartNew()
& $exe compile --fqbn $fqbn --libraries $libs --build-path $buildDir --warnings all $sketch 2>&1 |
  ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { Write-Host "Compile failed" -ForegroundColor Red; exit 1 }
$sw.Stop()
Write-Host ("Compile OK in {0:N1}s" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green

if ($Upload) {
  if (-not $Port) {
    $listOut = & $exe board list 2>&1
    $listOut | ForEach-Object { Write-Host "  $_" }
    $line = $listOut | Where-Object { "$_" -match 'COM\d+' } | Select-Object -First 1
    if ("$line" -match '(COM\d+)') {
      $Port = $matches[1]
      Write-Host "Using port: $Port" -ForegroundColor Yellow
    } else {
      Write-Host "No serial port found. Plug the board in, or pass -Port COMx" -ForegroundColor Red
      exit 1
    }
  }
  Write-Host ""
  Write-Host "=== Flashing to $Port ===" -ForegroundColor Cyan
  & $exe upload -p $Port --fqbn $fqbn --input-dir $buildDir $sketch 2>&1 | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) { Write-Host "Upload failed" -ForegroundColor Red; exit 1 }
  Write-Host "Flash OK" -ForegroundColor Green
}

if ($Monitor) {
  if (-not $Port) {
    $listOut = & $exe board list 2>&1
    $line = $listOut | Where-Object { "$_" -match 'COM\d+' } | Select-Object -First 1
    if ("$line" -match '(COM\d+)') { $Port = $matches[1] }
  }
  if (-not $Port) { Write-Host "No serial port found." -ForegroundColor Red; exit 1 }
  Write-Host ""
  Write-Host "=== Serial monitor on $Port (Ctrl+C to quit) ===" -ForegroundColor Cyan
  & $exe monitor -p $Port -c baudrate=115200
}

exit 0
