# build.ps1 —— 通过 USB 编译 / 烧录 AttitudeBall
#
# 用法：
#   .\build.ps1                     仅编译
#   .\build.ps1 -Upload             编译 + 烧录（自动探测串口）
#   .\build.ps1 -Upload -Port COM5  指定串口
#   .\build.ps1 -Clean              先删除构建目录再编译
#   .\build.ps1 -Monitor            编译后打开串口监视器（Ctrl+C 退出）
param(
  [switch]$Upload,
  [switch]$Clean,
  [switch]$Monitor,
  [string]$Port = ""
)

# 原生工具会把进度写到 stderr，用 Stop 会直接中断脚本。
$ErrorActionPreference = 'Continue'

$root     = 'E:\DeepSeekRuntime\APP\esp32s3-lcd185'
$exe      = Join-Path $root '_tools\arduino-cli\arduino-cli.exe'
$sketch   = Join-Path $root 'AttitudeBall'
$libs     = Join-Path $root '_libs'
$buildDir = Join-Path $root '_build'

# 整板定义 + 3MB APP 分区 + CDC 串口（这样 Serial 走 USB-C）
$fqbn = 'esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600'

if (-not (Test-Path $exe)) { Write-Host "未找到 arduino-cli：$exe" -ForegroundColor Red; exit 1 }

if ($Clean -and (Test-Path $buildDir)) {
  Write-Host "正在清理构建目录 ..." -ForegroundColor Yellow
  Remove-Item $buildDir -Recurse -Force
}

Write-Host "=== 开始编译 ===" -ForegroundColor Cyan
$sw = [System.Diagnostics.Stopwatch]::StartNew()
& $exe compile --fqbn $fqbn --libraries $libs --build-path $buildDir --warnings all $sketch 2>&1 |
  ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { Write-Host "编译失败" -ForegroundColor Red; exit 1 }
$sw.Stop()
Write-Host ("编译完成，用时 {0:N1}s" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green

if ($Upload) {
  if (-not $Port) {
    $listOut = & $exe board list 2>&1
    $listOut | ForEach-Object { Write-Host "  $_" }
    $line = $listOut | Where-Object { "$_" -match 'COM\d+' } | Select-Object -First 1
    if ("$line" -match '(COM\d+)') {
      $Port = $matches[1]
      Write-Host "使用串口：$Port" -ForegroundColor Yellow
    } else {
      Write-Host "未找到串口。请插上开发板，或用 -Port COMx 指定" -ForegroundColor Red
      exit 1
    }
  }
  Write-Host ""
  Write-Host "=== 烧录到 $Port ===" -ForegroundColor Cyan
  & $exe upload -p $Port --fqbn $fqbn --input-dir $buildDir $sketch 2>&1 | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) { Write-Host "烧录失败" -ForegroundColor Red; exit 1 }
  Write-Host "烧录成功" -ForegroundColor Green
}

if ($Monitor) {
  if (-not $Port) {
    $listOut = & $exe board list 2>&1
    $line = $listOut | Where-Object { "$_" -match 'COM\d+' } | Select-Object -First 1
    if ("$line" -match '(COM\d+)') { $Port = $matches[1] }
  }
  if (-not $Port) { Write-Host "未找到串口。" -ForegroundColor Red; exit 1 }
  Write-Host ""
  Write-Host "=== 串口监视器：$Port（Ctrl+C 退出）===" -ForegroundColor Cyan
  & $exe monitor -p $Port -c baudrate=115200
}

exit 0
