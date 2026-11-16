# pack_romfs.ps1 - Bundles Road Rash 30th Anniversary assets for Nintendo Switch
param(
    [string]$SourcePath = "C:\Users\schim\Downloads\road rash\Road Rash 30th Anniversary",
    [string]$DestRomfs = "C:\Users\schim\Downloads\road rash\switch-roadrash\romfs",
    [string]$SdCardDist = "C:\Users\schim\Downloads\road rash\sdcard_release\switch\roadrash"
)

Write-Host "=== Bundling Road Rash 30th Anniversary Assets for Nintendo Switch ===" -ForegroundColor Cyan

if (-not (Test-Path $SourcePath)) {
    Write-Error "Source path not found: $SourcePath"
    exit 1
}

# 1. Setup RomFS directory
New-Item -ItemType Directory -Force -Path "$DestRomfs\DATA" | Out-Null
New-Item -ItemType Directory -Force -Path "$DestRomfs\AUDIO" | Out-Null
New-Item -ItemType Directory -Force -Path "$DestRomfs\IMAGES" | Out-Null
New-Item -ItemType Directory -Force -Path "$DestRomfs\TEXT" | Out-Null

Write-Host "Copying core game data to RomFS..." -ForegroundColor Green
Copy-Item -Path "$SourcePath\DATA\*" -Destination "$DestRomfs\DATA" -Recurse -Force
Copy-Item -Path "$SourcePath\AUDIO\*" -Destination "$DestRomfs\AUDIO" -Recurse -Force
Copy-Item -Path "$SourcePath\IMAGES\*" -Destination "$DestRomfs\IMAGES" -Recurse -Force
Copy-Item -Path "$SourcePath\TEXT\*" -Destination "$DestRomfs\TEXT" -Recurse -Force
Copy-Item -Path "$SourcePath\RoadRash95.exe" -Destination "$DestRomfs\ROADRASH.EXE" -Force

Write-Host "RomFS packaging completed successfully!" -ForegroundColor Green

# 2. Setup SD Card distribution directory
if ($SdCardDist) {
    New-Item -ItemType Directory -Force -Path $SdCardDist | Out-Null
    Copy-Item "C:\Users\schim\Downloads\road rash\switch-roadrash\roadrash_switch.nro" -Destination "$SdCardDist\roadrash.nro" -Force
    Copy-Item "C:\Users\schim\Downloads\road rash\switch-roadrash\icon.jpg" -Destination "$SdCardDist\icon.jpg" -Force
    Write-Host "Created SD Card release ready to copy to Nintendo Switch at: $SdCardDist" -ForegroundColor Cyan
}
