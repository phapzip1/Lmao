# Downloads pinned prebuilt tools into tools/bin/ (gitignored). Safe to re-run: tools that are
# already at the pinned version are skipped. Linux: scripts/setup.sh.
#
#   powershell -ExecutionPolicy Bypass -File scripts/setup.ps1
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Invoke-WebRequest is much faster without the progress bar

$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root 'tools/bin'

# --- Slang (shader compiler, docs/decisions/0006) ----------------------------
# Keep in sync with scripts/setup.sh.
$SlangVersion = '2026.19'
$SlangAsset = "slang-$SlangVersion-windows-x86_64.zip"
$SlangSha256 = 'fc922f214b5d115f663932e59e80e0f6b6cc6fae140979cf307da6459b6f8f33'

function Install-Slang {
    $Dir = Join-Path $Bin 'slang'
    $VersionFile = Join-Path $Dir 'VERSION'
    if ((Test-Path $VersionFile) -and ((Get-Content $VersionFile -Raw).Trim() -eq $SlangVersion)) {
        Write-Host "slang ${SlangVersion}: already installed"
        return
    }

    $Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Path $Tmp | Out-Null
    try {
        $Archive = Join-Path $Tmp $SlangAsset
        Write-Host "slang ${SlangVersion}: downloading $SlangAsset"
        Invoke-WebRequest -Uri "https://github.com/shader-slang/slang/releases/download/v$SlangVersion/$SlangAsset" -OutFile $Archive

        $Actual = (Get-FileHash -Algorithm SHA256 $Archive).Hash.ToLowerInvariant()
        if ($Actual -ne $SlangSha256) {
            throw "SHA-256 mismatch for ${SlangAsset}: expected $SlangSha256, got $Actual"
        }

        if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }
        Expand-Archive -Path $Archive -DestinationPath $Dir
        Set-Content -Path $VersionFile -Value $SlangVersion
        Write-Host "slang ${SlangVersion}: installed to tools/bin/slang"
    }
    finally {
        Remove-Item -Recurse -Force $Tmp
    }
}

New-Item -ItemType Directory -Force -Path $Bin | Out-Null
Install-Slang
