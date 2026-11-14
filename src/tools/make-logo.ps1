# make-logo.ps1 - generate Boot_Logo.h from SVG / PNG / JPG (ASCII only: PS 5.1 mangles UTF-8 without BOM)
#
# Usage:
#   .\make-logo.ps1 logo.svg
#   .\make-logo.ps1 logo.png -Out my_logo.h
#
# Why SVG needs a browser:
#   The device cannot render vector graphics. Here the SVG is rasterized offline by
#   headless Chrome/Edge at 720x720 (2x), then downscaled to 360x360 by gen_logo.py,
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
if (-not $py -or -not (Test-Path $py)) { throw 'Python not found' }

if (-not (Test-Path $Source)) { throw "Source not found: $Source" }
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
    if (-not $browsers) { throw 'Need Chrome or Edge to rasterize SVG' }
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
    # A private --user-data-dir is required: otherwise the browser forwards the command
    # to an already running instance and exits without taking the screenshot.
    # Also pass exactly ONE target URL, or headless mode errors out.
    $bargs = @(
        '--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
        '--no-default-browser-check', "--user-data-dir=$profileDir",
        '--hide-scrollbars', "--window-size=$Size,$Size",
        '--virtual-time-budget=4000', "--screenshot=$tmpPng", $pageUri
    )
    Write-Host "Rasterizing SVG to ${Size}x${Size} PNG ..."
    $p = Start-Process -FilePath $browser -ArgumentList $bargs -NoNewWindow -PassThru -Wait
    if (-not (Test-Path $tmpPng)) { throw "SVG rasterize failed (exit code $($p.ExitCode))" }
    $kb = [math]::Round((Get-Item $tmpPng).Length / 1KB, 1)
    Write-Host "Rasterized: $kb KB"
    $inputFile = $tmpPng
}

Write-Host "Generating header: $Out"
& $py (Join-Path $tools 'gen_logo.py') $inputFile $Out
$code = $LASTEXITCODE

if ($work -and (Test-Path $work)) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
if ($code -ne 0) { throw "gen_logo.py failed (exit code $code)" }
Write-Host 'Done. Rebuild and flash to see the new logo.'
