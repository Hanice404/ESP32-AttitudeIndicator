# ota.ps1 -- Wireless OTA upload (no USB cable needed)
#
# Usage:
#   .\ota.ps1                    auto-discover device IP, upload built firmware
#   .\ota.ps1 -Build             compile first, then upload
#   .\ota.ps1 -Ip 192.168.50.59  specify IP manually
#   .\ota.ps1 -List              only list discoverable devices
#
# Requirements:
#   1) WIFI_SSID / WIFI_PASS filled in Config.h
#   2) Device powered on and joined to the same LAN
#   3) Windows Firewall allows espota inbound (see README section 6)
param(
  [switch]$Build,
  [switch]$List,
  [string]$Ip = "",
  [string]$Password = "attball"
)

# Native tools write progress to stderr; with Stop that would abort the script.
$ErrorActionPreference = 'Continue'

$root     = 'E:\DeepSeekRuntime\APP\esp32s3-lcd185'
$exe      = Join-Path $root '_tools\arduino-cli\arduino-cli.exe'
$sketch   = Join-Path $root 'AttitudeBall'
$libs     = Join-Path $root '_libs'
$buildDir = Join-Path $root '_build'
$fqbn     = 'esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600,DebugLevel=error'

if (-not (Test-Path $exe)) { Write-Host "arduino-cli not found: $exe" -ForegroundColor Red; exit 1 }

# ---- 1. discover network devices ----
Write-Host "=== Searching for network devices (mDNS, 5s) ===" -ForegroundColor Cyan
$listOut = & $exe board list --discovery-timeout 5s 2>&1
$listOut | ForEach-Object { Write-Host "  $_" }

if ($List) { exit 0 }

if (-not $Ip) {
  $line = $listOut | Where-Object { "$_" -match 'network' } | Select-Object -First 1
  if ("$line" -match '(\d+\.\d+\.\d+\.\d+)') {
    $Ip = $matches[1]
    Write-Host "Auto-discovered device IP: $Ip" -ForegroundColor Yellow
  } else {
    Write-Host "No network device found. Check: 1) device powered + on WiFi  2) same LAN  3) or pass -Ip" -ForegroundColor Red
    exit 1
  }
}

# ---- 2. optional compile ----
if ($Build) {
  Write-Host ""
  Write-Host "=== Compiling ===" -ForegroundColor Cyan
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  & $exe compile --fqbn $fqbn --libraries $libs --build-path $buildDir --warnings all $sketch 2>&1 |
    ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) { Write-Host "Compile failed" -ForegroundColor Red; exit 1 }
  $sw.Stop()
  Write-Host ("Compile OK in {0:N1}s" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
}

$bin = Join-Path $buildDir 'AttitudeBall.ino.bin'
if (-not (Test-Path $bin)) {
  Write-Host "No compiled firmware found. Run with -Build first." -ForegroundColor Red
  exit 1
}

# ---- 3. upload over WiFi ----
Write-Host ""
Write-Host "=== Uploading to $Ip over WiFi ===" -ForegroundColor Cyan

# Filter arduino-cli progress spam: show only 10% steps and key lines.
$lastPct = -10
& $exe upload --protocol network -p $Ip --fqbn $fqbn --upload-field "password=$Password" --input-dir $buildDir $sketch 2>&1 |
  ForEach-Object {
    $s = "$_"
    if ($s -match 'System\.Management\.Automation\.RemoteException') { return }
    if ($s -match 'Uploading:\s*\[[^\]]*\]\s*(\d+)%') {
      $pct = [int]$matches[1]
      if ($pct -ge $lastPct + 10) {
        $lastPct = $pct
        Write-Host ("  {0,3}%" -f $pct)
      }
      return
    }
    if ($s.Trim()) { Write-Host "  $s" }
  }

if ($LASTEXITCODE -ne 0) {
  Write-Host ""
  Write-Host "Upload FAILED. Common causes:" -ForegroundColor Yellow
  Write-Host "  * Windows Firewall blocks espota inbound TCP  (see README section 6)"
  Write-Host "  * Device IP changed                           (re-run or pass -Ip)"
  Write-Host "  * OTA password mismatch with Config.h OTA_PASSWORD"
  exit 1
}

Write-Host ""
Write-Host "OTA done -- device rebooted into the new firmware." -ForegroundColor Green
exit 0
