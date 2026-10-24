#!/usr/bin/env bash
# Downloads pinned prebuilt tools into tools/bin/ (gitignored). Safe to re-run: tools that are
# already at the pinned version are skipped. Windows: scripts/setup.ps1.
#
#   scripts/setup.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/tools/bin"

# --- Slang (shader compiler, docs/decisions/0006) ----------------------------
# Keep in sync with scripts/setup.ps1.
SLANG_VERSION="2026.19"
SLANG_ASSET="slang-${SLANG_VERSION}-linux-x86_64-glibc-2.28.tar.gz"
SLANG_SHA256="bd7afcb0956dee9304f44ed115e9aaa7140704271b5c3294bcb237e3ae67adc7"

install_slang() {
    local dir="$BIN/slang"
    if [[ -f "$dir/VERSION" && "$(cat "$dir/VERSION")" == "$SLANG_VERSION" ]]; then
        echo "slang $SLANG_VERSION: already installed"
        return
    fi

    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN

    echo "slang $SLANG_VERSION: downloading $SLANG_ASSET"
    curl -fL --retry 3 -o "$tmp/$SLANG_ASSET" \
        "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_ASSET}"
    echo "$SLANG_SHA256  $tmp/$SLANG_ASSET" | sha256sum -c --quiet -

    rm -rf "$dir"
    mkdir -p "$dir"
    tar -xzf "$tmp/$SLANG_ASSET" -C "$dir"
    echo "$SLANG_VERSION" > "$dir/VERSION"
    echo "slang $SLANG_VERSION: installed to tools/bin/slang"
}

mkdir -p "$BIN"
install_slang
