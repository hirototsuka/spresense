#!/usr/bin/env bash
# overlay/ 以下の独自コードを SDK ディレクトリへ上書きコピーする
#
#   使い方: ./scripts/apply_overlay.sh [SDKディレクトリ]   (既定: ~/spresense-backup-developSITv330)

set -euo pipefail

SDK_DIR="${1:-$HOME/spresense-backup-developSITv330}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -d "$SDK_DIR/sdk" ]; then
  echo "エラー: $SDK_DIR は Spresense SDK ではありません（sdk/ がありません）" >&2
  exit 1
fi

echo "== overlay を $SDK_DIR に適用します"
cp -rv "$REPO_ROOT/overlay/." "$SDK_DIR/"
