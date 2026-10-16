#!/usr/bin/env bash
# audiolite_wavrecorder をデジタルマイク設定でビルドする
# （手順書の config → menuconfig(MICID変更) → make を自動化したもの）
#
#   使い方: ./scripts/build_dmic.sh [SDKディレクトリ] [マイク数]
#           SDKディレクトリ 既定: ~/spresense-backup-developSITv330
#           マイク数        既定: 8（CH0〜7 = デジタルマイク0〜7 = MICID 5〜12）
#
#   ※ overlay の録音コードは 8ch 固定（NUM_CHANNELS 8）。
#     マイク数を 8 未満にする場合は audiolite_wavrecorder_main.cxx の
#     NUM_CHANNELS も同じ数に変更すること。

set -euo pipefail

SDK_DIR="${1:-$HOME/spresense-backup-developSITv330}"
NUM_MICS="${2:-8}"

if [ "$NUM_MICS" -lt 1 ] || [ "$NUM_MICS" -gt 8 ]; then
  echo "エラー: マイク数は 1〜8 で指定してください" >&2
  exit 1
fi

source "$HOME/spresenseenv/setup"
cd "$SDK_DIR/sdk"

make distclean
./tools/config.py default examples/audiolite_wavrecorder

# MICID: 使うチャンネルはデジタルマイク(5〜12)、残りは未使用(15)
CONFIG=../nuttx/.config
for ch in 0 1 2 3 4 5 6 7; do
  if [ "$ch" -lt "$NUM_MICS" ]; then id=$((ch + 5)); else id=15; fi
  sed -i "s/^CONFIG_CXD5247_MICID_CH$ch=.*/CONFIG_CXD5247_MICID_CH$ch=$id/" "$CONFIG"
done
make olddefconfig
echo "== マイク設定"
grep MICID "$CONFIG"

make
echo "== ビルド完了: $SDK_DIR/sdk/nuttx.spk"
echo "   書き込み: ./tools/flash.sh -c /dev/ttyUSB0 nuttx.spk"
