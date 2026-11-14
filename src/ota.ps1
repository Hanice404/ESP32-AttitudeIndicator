# ota.ps1 —— 无线 OTA 烧录（不需要插 USB 线）
#
# 用法：
#   .\ota.ps1                    自动发现设备 IP，上传已编译固件
#   .\ota.ps1 -Build             先编译再上传
#   .\ota.ps1 -Ip 192.168.50.59  手动指定 IP
#   .\ota.ps1 -List              只列出可发现的设备
#
# 前提条件：
#   1) Config.h 里的 WIFI_SSID / WIFI_PASS 已填写
#   2) 设备已上电并连到同一局域网
#   3) Windows 防火墙允许 espota 入站（见 README 第 6 节）
param(
  [switch]$Build,
  [switch]$List,
  [string]$Ip = "",
  [string]$Password = "attball"
)

# 原生工具会把进度写到 stderr，用 Stop 会直接中断脚本。
$ErrorActionPreference = 'Continue'

$root     = 'E:\DeepSeekRuntime\APP\esp32s3-lcd185'
$exe      = Join-Path $root '_tools\arduino-cli\arduino-cli.exe'
$sketch   = Join-Path $root 'AttitudeBall'
$libs     = Join-Path $root '_libs'
$buildDir = Join-Path $root '_build'
$fqbn     = 'esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600,DebugLevel=error'

if (-not (Test-Path $exe)) { Write-Host "未找到 arduino-cli：$exe" -ForegroundColor Red; exit 1 }

# ---- 1. 发现网络设备 ----
Write-Host "=== 搜索网络设备（mDNS，5 秒）===" -ForegroundColor Cyan
$listOut = & $exe board list --discovery-timeout 5s 2>&1
$listOut | ForEach-Object { Write-Host "  $_" }

if ($List) { exit 0 }

if (-not $Ip) {
  $line = $listOut | Where-Object { "$_" -match 'network' } | Select-Object -First 1
  if ("$line" -match '(\d+\.\d+\.\d+\.\d+)') {
    $Ip = $matches[1]
    Write-Host "自动发现设备 IP：$Ip" -ForegroundColor Yellow
  } else {
    Write-Host "未发现网络设备。请检查：1) 设备已上电并连上 WiFi  2) 在同一局域网  3) 或用 -Ip 指定" -ForegroundColor Red
    exit 1
  }
}

# ---- 2. 可选：先编译 ----
if ($Build) {
  Write-Host ""
  Write-Host "=== 开始编译 ===" -ForegroundColor Cyan
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  & $exe compile --fqbn $fqbn --libraries $libs --build-path $buildDir --warnings all $sketch 2>&1 |
    ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) { Write-Host "编译失败" -ForegroundColor Red; exit 1 }
  $sw.Stop()
  Write-Host ("编译完成，用时 {0:N1}s" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
}

$bin = Join-Path $buildDir 'AttitudeBall.ino.bin'
if (-not (Test-Path $bin)) {
  Write-Host "没有找到已编译的固件。请先加 -Build 参数运行一次。" -ForegroundColor Red
  exit 1
}

# ---- 3. 通过 WiFi 上传 ----
Write-Host ""
Write-Host "=== 正在通过 WiFi 上传到 $Ip ===" -ForegroundColor Cyan

# 过滤 arduino-cli 的进度刷屏：只显示 10% 档位和关键行。
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
  Write-Host "上传失败。常见原因：" -ForegroundColor Yellow
  Write-Host "  * Windows 防火墙拦截了 espota 入站 TCP（见 README 第 6 节）"
  Write-Host "  * 设备 IP 变了                              （重新运行或加 -Ip）"
  Write-Host "  * OTA 密码与 Config.h 里的 OTA_PASSWORD 不一致"
  exit 1
}

Write-Host ""
Write-Host "OTA 完成 —— 设备已重启进入新固件。" -ForegroundColor Green
exit 0
