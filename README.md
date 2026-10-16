# spresense-dmic-recorder

Spresense のデジタルマイク（最大8ch）で録音し、SDカードに WAV で保存するための独自コードと手順。
公式 [Spresense SDK](https://github.com/sonydevworld/spresense) **v3.3.0** に、このリポジトリの `overlay/` を重ねて使う。

## このリポジトリに入っているもの

| パス | 内容 |
|---|---|
| `overlay/examples/audiolite_wavrecorder/` | 8ch録音アプリ（48kHz / 16bit / 8ch、録音秒数を引数で指定、メタデータ `*_md.txt` も出力） |
| `overlay/sdk/modules/audiolite/src/components/al_inputcomp.cxx` | audiolite の入力上限を 4ch → **8ch** に変更（`MIC_MAXCH 8`） |
| `overlay/myapps/audiofilesender/` | 録音ファイル送信アプリ（PC側の受信スクリプト `receiver.py` 付き） |
| `overlay/myapps/audiofilestreamer/` | 録音ストリーミングアプリ |
| `scripts/setup_sdk.sh` | 公式SDK v3.3.0 の取得 ＋ overlay の適用 |
| `scripts/apply_overlay.sh` | 既存のSDKに overlay だけ適用 |
| `scripts/build_dmic.sh` | デジタルマイク設定（MICID）でビルド |

開発環境（ツールチェーン、約700MB）とSDK本体は含めていない。下の手順で入手する。

---

## 全体の流れ

```
[初回のみ] 0. WSL/USB の準備 → 1. 開発環境 → 2. SDK取得 → 3. ローダー書き込み
[毎回]     4. ボード接続 → 5. ビルド → 6. 書き込み → 7. 録音
```

### 0. WSL / USB の準備（初回のみ）

Windows + WSL2 (Ubuntu) を前提とする。

```bash
# WSL: シリアルポートを使えるようにする（実行後に WSL を開き直す）
sudo usermod -aG dialout $USER
```

```powershell
# Windows: 「管理者」PowerShell でボードを共有する
usbipd list                  # CP210x の BUSID を確認（例: 2-2）
usbipd bind --busid 2-2
```

### 1. 開発環境（初回のみ）

公式SDKに含まれる `install-tools.sh` で `~/spresenseenv` にツールチェーンを入れる（2. の後でも可）。

```bash
cd ~/spresense-backup-developSITv330 && ./install-tools.sh
```

以降、**新しいターミナルを開いたら毎回**次を実行する。

```bash
source ~/spresenseenv/setup
```

### 2. SDK の取得（初回のみ）

```bash
git clone https://github.com/hirototsuka/spresense-dmic-recorder.git
cd spresense-dmic-recorder
./scripts/setup_sdk.sh      # ~/spresense-backup-developSITv330 に公式 v3.3.0 を取得して overlay を適用
```

`~/spresense-backup-developSITv330` が既にある場合は、取得は不要で overlay だけ適用する。

```bash
./scripts/apply_overlay.sh  # ~/spresense-backup-developSITv330 に overlay を適用
```

### 3. v3.3.0 ローダーの書き込み（初回のみ）

[Spresense ファームウェア v3.3.0](https://developer.sony.com/file/download/download-spresense-firmware-v3-3-0) の zip を取得してから:

```bash
source ~/spresenseenv/setup
cd ~/spresense-backup-developSITv330/sdk
./tools/flash.sh -e ~/spresense-binaries-v3.3.0.zip
./tools/flash.sh -l ~/spresense-backup-developSITv330/firmware/spresense -c /dev/ttyUSB0
```

### 4. ボードを WSL に接続（PC再起動・USB抜き差しのたび）

```powershell
# Windows: 通常の PowerShell
usbipd attach --wsl --busid 2-2
```

```bash
ls /dev/ttyUSB*      # /dev/ttyUSB0 が見えればOK
```

### 5. ビルド

```bash
./scripts/build_dmic.sh        # ~/spresense-backup-developSITv330 を 8ch（MICID CH0〜7 = 5〜12）
```

手作業でやる場合（スクリプトと同じ内容）:

```bash
source ~/spresenseenv/setup
cd ~/spresense-backup-developSITv330/sdk/
make distclean
./tools/config.py default examples/audiolite_wavrecorder
make menuconfig    # '/' で micid を検索し、CH0〜7 を 5,6,7,8,9,10,11,12 に変更して保存
make
```

> `config.py` はマイク設定を初期値（1,2,3,4,15…＝アナログ）に戻すので、MICID の変更は必ず `config.py` の **後** に行う。

### 6. 書き込み

```bash
cd ~/spresense-backup-developSITv330/sdk
./tools/flash.sh -c /dev/ttyUSB0 nuttx.spk
```

### 7. 録音

```bash
screen /dev/ttyUSB0 115200      # 終了: Ctrl+A → \ → y
```

```
nsh> ls /mnt                                     # sd0/ が見えるまで待つ（起動後数秒）
nsh> audiolite_wavrecorder /mnt/sd0/test 10      # 10秒録音
```

→ `/mnt/sd0/test_00.wav`（48kHz / 16bit / 8ch）と `/mnt/sd0/test_md.txt` ができる。

> 同じプレフィックスの既存ファイル（`test_00.wav` 等）は**録音前に自動削除される**。残したいデータと同じ名前を使わないこと。

---

## マイク設定（MICID）

| MICID | 意味 |
|---|---|
| 1〜4 | アナログマイク A〜D |
| 5〜12 | デジタルマイク 0〜7 |
| 15 | 未使用 |

ドライバが受け付ける最大チャンネル数は「15 以外の MICID の数」。アプリが要求するチャンネル数（`NUM_CHANNELS`）以上にしておくこと。

## トラブルシューティング

| 症状 | 原因の目安 |
|---|---|
| `config.py` が `../nuttx/tools/Config.mk: No such file or directory` で失敗 | SDK のサブモジュール（`nuttx/` 等）が空。zip で取得すると空になる。`git clone --recursive` で取得する |
| `Cannot open port : /dev/ttyUSB0` | `dialout` 未所属、または `screen` がポートを使用中（`screen -ls` で残ったセッションを確認） |
| `Start error..: -1` | SDカードのマウント前に実行した。`ls /mnt` で `sd0` を確認してから実行 |
| 全チャンネルが一定値（最初 -32768 付近 → 0 に収束） | マイクのデータ線が止まっている。配線・電源・クロックを確認 |
| L と R がサンプル単位で完全一致 | 同じデータ線上で実際に信号を出しているマイクが1個だけ。2個目の L/R 選択ピン・電源・配線を確認 |
