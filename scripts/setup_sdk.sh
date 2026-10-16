#!/usr/bin/env bash
# 公式 Spresense SDK v3.3.0 を取得し、このリポジトリの独自コード（overlay）を重ねる
#
#   使い方: ./scripts/setup_sdk.sh [SDKを置くディレクトリ]   (既定: ~/spresense)

set -euo pipefail

SDK_DIR="${1:-$HOME/spresense}"
SDK_TAG="v3.3.0"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ -e "$SDK_DIR" ]; then
  echo "エラー: $SDK_DIR は既に存在します。別のディレクトリを指定してください。" >&2
  exit 1
fi

echo "== 公式SDK $SDK_TAG を取得します: $SDK_DIR"
git clone --recursive -b "$SDK_TAG" https://github.com/sonydevworld/spresense.git "$SDK_DIR"

"$REPO_ROOT/scripts/apply_overlay.sh" "$SDK_DIR"

echo "== 完了しました。次は ./scripts/build_dmic.sh $SDK_DIR でビルドできます"
