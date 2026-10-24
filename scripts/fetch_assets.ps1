# Downloads test models from KhronosGroup/glTF-Sample-Assets into assets/external/ (gitignored),
# pinned to one commit. Only the requested models are checked out (sparse, blobless clone).
# Safe to re-run. Linux: scripts/fetch_assets.sh.
#
#   powershell -ExecutionPolicy Bypass -File scripts/fetch_assets.ps1            # DamagedHelmet
#   powershell -ExecutionPolicy Bypass -File scripts/fetch_assets.ps1 Sponza     # any model folder name(s)
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$Dest = Join-Path $Root 'assets/external/glTF-Sample-Assets'

# Keep in sync with scripts/fetch_assets.sh.
$Repository = 'https://github.com/KhronosGroup/glTF-Sample-Assets.git'
$Commit = 'edc7c9e67c639d230715049ee31f9a96a6babbbe' # 2026-10-02

$Models = if ($args.Count -gt 0) { $args } else { @('DamagedHelmet') }

function Invoke-Git {
    git @args
    if ($LASTEXITCODE -ne 0) { throw "git $args failed" }
}

if (-not (Test-Path (Join-Path $Dest '.git'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dest) | Out-Null
    Invoke-Git clone --filter=blob:none --no-checkout --sparse $Repository $Dest
}

$Paths = $Models | ForEach-Object { "Models/$_" }
Invoke-Git -C $Dest sparse-checkout add @Paths
Invoke-Git -C $Dest fetch --quiet --filter=blob:none origin $Commit
Invoke-Git -C $Dest -c advice.detachedHead=false checkout --quiet $Commit

foreach ($Model in $Models) {
    if (Test-Path (Join-Path $Dest "Models/$Model")) {
        Write-Host "${Model}: assets/external/glTF-Sample-Assets/Models/$Model"
    } else {
        throw "${Model}: not found in glTF-Sample-Assets at $Commit"
    }
}
