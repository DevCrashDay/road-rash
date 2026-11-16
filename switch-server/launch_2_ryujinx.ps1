param(
    [string]$ServerUrl = "ws://127.0.0.1:8080/net",
    [string]$RyujinxExe = "C:\Users\schim\Downloads\ryujinx-1.3.3-win_x64\publish\Ryujinx.exe",
    [string]$NroSource = "$PSScriptRoot\..\switch-roadrash\roadrash_switch.nro",
    [string]$RyujinxBaseData = "$env:APPDATA\Ryujinx"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $RyujinxExe)) { throw "Ryujinx not found: $RyujinxExe" }
if (-not (Test-Path $NroSource)) { throw "NRO not found: $NroSource" }

$baseCfg = Get-Content "$RyujinxBaseData\Config.json" -Raw | ConvertFrom-Json
$baseCfg.enable_internet_access = $true
$baseCfg.disable_input_when_out_of_focus = $true
$baseCfg.enable_discord_integration = $false
$baseCfg.show_confirm_exit = $false
$baseCfg.show_console = $false

$instances = @(
    @{ Dir = "$PSScriptRoot\.ryu-p1"; Alias = "RyujinxP1"; Nro = "roadrash_p1.nro"; X = 20;  Y = 40 },
    @{ Dir = "$PSScriptRoot\.ryu-p2"; Alias = "RyujinxP2"; Nro = "roadrash_p2.nro"; X = 840; Y = 40 }
)

foreach ($inst in $instances) {
    $dir = $inst.Dir
    New-Item -ItemType Directory -Force $dir | Out-Null

    # Copy per-instance bis/system/profiles so Ryujinx never hits ResultFsTargetLocked
    foreach ($sub in @("bis", "system", "profiles")) {
        $dstSub = Join-Path $dir $sub
        $srcSub = Join-Path $RyujinxBaseData $sub
        if (-not (Test-Path $dstSub) -and (Test-Path $srcSub)) {
            Copy-Item $srcSub $dstSub -Recurse -Force
        }
    }
    foreach ($f in @("Profiles.json", "prod.keys", "title.keys")) {
        $srcF = Join-Path $RyujinxBaseData $f
        if ((Test-Path $srcF) -and -not (Test-Path (Join-Path $dir $f))) {
            Copy-Item $srcF (Join-Path $dir $f) -Force
        }
    }

    $cfg = $baseCfg | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    $cfg.window_startup.window_position_x = $inst.X
    $cfg.window_startup.window_position_y = $inst.Y
    $cfg.window_startup.window_size_width = 800
    $cfg.window_startup.window_size_height = 490
    $cfg | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $dir "Config.json") -Encoding UTF8

    $rrDir = Join-Path $dir "sdcard\switch\roadrash"
    New-Item -ItemType Directory -Force $rrDir | Out-Null
    $digestSrc = Join-Path $RyujinxBaseData "sdcard\switch\roadrash\net_digest_v2.bin"
    $digestDst = Join-Path $rrDir "net_digest_v2.bin"
    if ((Test-Path $digestSrc) -and -not (Test-Path $digestDst)) {
        Copy-Item $digestSrc $digestDst -Force
    }
    @"
local_players=1
alias=$($inst.Alias)
net_server=$ServerUrl
"@ | Set-Content (Join-Path $rrDir "settings.txt") -Encoding ASCII

    # Copy NRO per instance so Ryujinx's exclusive file lock never blocks Player 2
    $nroDst = Join-Path $dir $inst.Nro
    Copy-Item $NroSource $nroDst -Force

    Start-Process -FilePath $RyujinxExe -ArgumentList "-r `"$dir`" `"$nroDst`""
    Start-Sleep -Seconds 1
}

Write-Host "Launched RyujinxP1 and RyujinxP2 -> $ServerUrl"
Write-Host "In both windows: press Z through Intro/Credits -> Main Menu -> Mano a Mano -> Host (P1) / Room List (P2)."
