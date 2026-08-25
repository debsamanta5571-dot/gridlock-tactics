# Package a playable Win64 desktop build (Development config).
# Output: Packaged/Win64/TacticsGameUnreal.exe (+ Engine/ content folder)
$ErrorActionPreference = "Stop"

$repo = Split-Path $PSScriptRoot -Parent
$uproject = Join-Path $repo "TacticsGameUnreal 5.8\TacticsGameUnreal.uproject"
$archive = Join-Path $repo "Packaged\Win64"
$uat = "E:\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat"

if (-not (Test-Path $uat)) {
    throw "UE 5.8 not found at E:\UE_5.8. Install UE 5.8 or edit scripts/package_win64_desktop.ps1."
}

New-Item -ItemType Directory -Path $archive -Force | Out-Null

& $uat BuildCookRun `
    -project="$uproject" `
    -noP4 `
    -installed `
    -platform=Win64 `
    -clientconfig=Development `
    -serverconfig=Development `
    -target=TacticsGameUnreal `
    -cook `
    -build `
    -stage `
    -pak `
    -archive `
    -archivedirectory="$archive"

if ($LASTEXITCODE -ne 0) {
    throw "Packaging failed with exit code $LASTEXITCODE"
}

$stagedData = Join-Path $archive "TacticsGameUnreal\Content\TacticsData"
$catalog = Join-Path $stagedData "ability_catalog.json"
$tokenArt = Join-Path $stagedData "card_art\token\token.png"
$paks = Join-Path $archive "TacticsGameUnreal\Content\Paks"
$exe = Join-Path $archive "TacticsGameUnreal.exe"
if (-not (Test-Path $catalog)) {
    throw "Packaged build is missing Content/TacticsData (JSON/art). Expected: $catalog"
}
if (-not (Test-Path $tokenArt)) {
    throw "Packaged build is missing card art PNGs. Expected: $tokenArt"
}
if (-not (Test-Path $paks)) {
    throw "Packaged build is missing Content/Paks (cooked maps/materials)."
}
if (-not (Test-Path $exe)) {
    throw "Packaged build is missing TacticsGameUnreal.exe"
}
Write-Host "Staged TacticsData: $stagedData"
Write-Host "Playable build: $exe"
Write-Host "Keep the whole Packaged\Win64 folder together (Engine + TacticsGameUnreal)."