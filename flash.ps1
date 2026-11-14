# flash.ps1 —— 一键烧录预编译固件
#
# 用法：
#   .\flash.ps1                 交互式，列出串口后提示输入
#   .\flash.ps1 -Port COM6      指定串口
#
# 注意：本文件必须保存为「UTF-8 带 BOM」，否则 Windows PowerShell 5.1
# 会按 ANSI 解析中文注释而导致语法错误。
param(
    [string]$Port = ""
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin = Join-Path $here 'firmware\attitude-indicator-merged.bin'

if (-not (Test-Path $bin)) { throw "未找到固件：$bin" }

if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Host '当前可用的串口：'
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports) { $ports | ForEach-Object { Write-Host "  $_" } } else { Write-Host '  （没有找到串口）' }
    $Port = Read-Host '请输入串口名（例如 COM6）'
}
if ([string]::IsNullOrWhiteSpace($Port)) { throw '没有输入串口' }

# 查找 esptool：优先使用 ESP32 Arduino 核心自带的，其次用 PATH 里的
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
    Write-Host '没有找到 esptool，请先安装：' -ForegroundColor Yellow
    Write-Host '    pip install esptool' -ForegroundColor Yellow
    throw 'esptool 未安装'
}

Write-Host ''
Write-Host "固件   : $bin"
Write-Host "串口   : $Port"
Write-Host "esptool: $esptool"
Write-Host ''
Write-Host '如果烧录失败：按住 BOOT，点一下 RESET，松开 BOOT，然后重试。' -ForegroundColor Cyan
Write-Host ''

& $esptool --chip esp32s3 -p $Port -b 921600 write_flash 0x0 $bin
if ($LASTEXITCODE -ne 0) { throw "烧录失败（退出码 $LASTEXITCODE）" }

Write-Host ''
Write-Host '完成。开发板会自动重启进入姿态仪。' -ForegroundColor Green
Write-Host '提示：把它摆成你认为"水平"的姿态，短按一次 BOOT 即可对准地平线。' -ForegroundColor Green
