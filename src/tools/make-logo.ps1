# make-logo.ps1 —— 从 SVG / PNG / JPG 生成 Boot_Logo.h（本文件必须带 BOM，否则 PS 5.1 会乱码）
#
# 用法：
#   .\make-logo.ps1 logo.svg
#   .\make-logo.ps1 logo.png -Out my_logo.h
#
# 为什么 SVG 需要浏览器：
#   设备无法渲染矢量图，所以在电脑端用无头 Chrome/Edge 以 720×720（2 倍）光栅化，
#   再由 gen_logo.py 缩小到 360×360 —— 相当于离线抗锯齿，边缘更平滑。
#   which gives smooth anti-aliased edges.
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Out = "",
    [int]$Size = 720
)

$ErrorActionPreference = 'Stop'
$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path -Parent $tools
if ([string]::IsNullOrWhiteSpace($Out)) { $Out = Join-Path $proj 'AttitudeBall\Boot_Logo.h' }

$py = 'C:\Users\Administrator\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
if (-not (Test-Path $py)) {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { $py = $cmd.Source }
}
if (-not $py -or -not (Test-Path $py)) { throw '未找到 Python' }

if (-not (Test-Path $Source)) { throw "源文件不存在：$Source" }
$Source = (Resolve-Path $Source).Path
$ext = [System.IO.Path]::GetExtension($Source).ToLower()
$work = $null
$inputFile = $Source

if ($ext -eq '.svg') {
    $browsers = @(
        'C:\Program Files\Google\Chrome\Application\chrome.exe',
        'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe',
        'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
        'C:\Program Files\Microsoft\Edge\Application\msedge.exe'
    ) | Where-Object { Test-Path $_ }
    if (-not $browsers) { throw '需要 Chrome 或 Edge 来把 SVG 转成位图' }
    $browser = $browsers[0]

    $work = Join-Path $env:TEMP ('svglogo_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $work | Out-Null

    $svgUri = 'file:///' + ($Source -replace '\\', '/')
    $html = "<!doctype html><html><body style='margin:0;padding:0;background:#000;overflow:hidden'>" +
            "<img src='$svgUri' style='width:${Size}px;height:${Size}px;object-fit:contain;display:block'></body></html>"
    $htmlPath = Join-Path $work 'page.html'
    Set-Content -Path $htmlPath -Value $html -Encoding UTF8
    $pageUri = 'file:///' + ($htmlPath -replace '\\', '/')

    $tmpPng = Join-Path $work 'logo.png'
    $profileDir = Join-Path $work 'profile'
    # 必须给独立的 --user-data-dir：否则浏览器会把命令转给已运行的实例，
    # 直接退出而不生成截图。
    # 另外只能传一个目标 URL，否则无头模式会报 Multiple targets 错误。
    $bargs = @(
        '--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--no-default-browser-check', "--user-data-dir=$profileDir",
        '--hide-scrollbars', "--window-size=$Size,$Size",
        '--virtual-time-budget=4000', "--screenshot=$tmpPng", $pageUri
    )
    Write-Host "正在把 SVG 渲染成 ${Size}x${Size} PNG ..."
    $p = Start-Process -FilePath $browser -ArgumentList $bargs -NoNewWindow -PassThru -Wait
    if (-not (Test-Path $tmpPng)) { throw "SVG 光栅化失败（退出码 $($p.ExitCode)）" }
    $kb = [math]::Round((Get-Item $tmpPng).Length / 1KB, 1)
    Write-Host "光栅化完成：$kb KB"
    $inputFile = $tmpPng
}

Write-Host "正在生成头文件：$Out"
# Python 输出是 UTF-8，而中文控制台默认 GBK，会乱码 —— 这里临时切到 UTF-8
$prevEnc = [Console]::OutputEncoding
$prevPyEnc = $env:PYTHONIOENCODING
try {
    [Console]::OutputEncoding = [Text.Encoding]::UTF8
    $env:PYTHONIOENCODING = 'utf-8'
    & $py (Join-Path $tools 'gen_logo.py') $inputFile $Out
    $code = $LASTEXITCODE
} finally {
    [Console]::OutputEncoding = $prevEnc
    $env:PYTHONIOENCODING = $prevPyEnc
}

if ($work -and (Test-Path $work)) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
if ($code -ne 0) { throw "gen_logo.py 失败（退出码 $code）" }
Write-Host '完成。重新编译烧录即可看到新 Logo。'
