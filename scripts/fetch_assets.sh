#!/usr/bin/env bash
# Downloads test models from KhronosGroup/glTF-Sample-Assets into assets/external/ (gitignored),
# pinned to one commit. Only the requested models are checked out (sparse, blobless clone).
# Safe to re-run. Windows: scripts/fetch_assets.ps1.
#
#   scripts/fetch_assets.sh                 # DamagedHelmet (default, ~4 MB)
#   scripts/fetch_assets.sh Sponza          # any model folder name(s) from the repository
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/assets/external/glTF-Sample-Assets"

# Keep in sync with scripts/fetch_assets.ps1.
REPOSITORY="https://github.com/KhronosGroup/glTF-Sample-Assets.git"
COMMIT="edc7c9e67c639d230715049ee31f9a96a6babbbe" # 2026-10-02

MODELS=("$@")
[[ ${#MODELS[@]} -eq 0 ]] && MODELS=(DamagedHelmet)

if [[ ! -d "$DEST/.git" ]]; then
    mkdir -p "$(dirname "$DEST")"
    git clone --filter=blob:none --no-checkout --sparse "$REPOSITORY" "$DEST"
fi

# Add the requested models to what's already checked out.
paths=()
for model in "${MODELS[@]}"; do paths+=("Models/$model"); done
git -C "$DEST" sparse-checkout add "${paths[@]}"
git -C "$DEST" fetch --quiet --filter=blob:none origin "$COMMIT"
git -C "$DEST" -c advice.detachedHead=false checkout --quiet "$COMMIT"

for model in "${MODELS[@]}"; do
    if [[ -d "$DEST/Models/$model" ]]; then
        echo "$model: assets/external/glTF-Sample-Assets/Models/$model"
    else
        echo "$model: not found in glTF-Sample-Assets at $COMMIT" >&2
        exit 1
    fi
done
