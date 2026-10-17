[![twitter spresense handle][]][twitter spresense badge]
[![twitter devworld handle][]][twitter devworld badge]

# SPRESENSE プロジェクトへようこそ

このリポジトリをクローンし、サブモジュールを更新します。

```
$ git clone --recursive https://github.com/sonydevworld/spresense.git
```

# サブモジュール

```
spresense                  - このリポジトリ
|-- nuttx                  - NuttX 本家のカーネル + SPRESENSE 向け移植
|-- sdk
|   `-- apps               - NuttX 本家のアプリケーション + SPRESENSE 向け移植
`-- externals
    `-- nnablart
      `-- nnabla-c-runtime - ニューラルネットワーク実行ライブラリ
```

## SDK2.x.x から SDK3.x.x への更新

SDK3.0.0 以降、spresense リポジトリのサブモジュール（nuttx、sdk/apps）の URL が変更されました。SDK2.x 以前にリポジトリをクローンした場合は、次の手順でサブモジュールの URL を更新してください。

```
$ cd spresense
$ git fetch origin
$ git checkout <remote branch>
$ git submodule sync
$ git submodule update
```
* `<remote branch>`
  * master ブランチの場合: `origin/master`
  * develop ブランチの場合: `origin/develop`

# Spresense SDK のビルド手順

ビルド手順は [Spresense SDK スタートガイド](https://developer.sony.com/develop/spresense/docs/sdk_set_up_en.html) に記載されています。

## 事前準備

クロスコンパイルに必要なパッケージと GCC ARM ツールチェーンをインストールします。
```
$ wget https://raw.githubusercontent.com/sonydevworld/spresense/master/install-tools.sh
$ bash install-tools.sh
```

## ビルド

{SDK_FULL} をクローンしたフォルダに移動し、`sdk` フォルダに入ります。
``` bash
$ cd spresense/sdk
```
SDK の設定（コンフィグ）を行います。
``` bash
$ tools/config.py examples/hello
```
サンプルのイメージをビルドします。
``` bash
$ make
```

この手順が正常に終わると、`sdk` フォルダに `nuttx.spk` ファイルができます。
このファイルが最終的な成果物で、ボードに書き込むことができます。

# Docker を使う場合

Spresense SDK のビルドに必要なものがすべて揃った、ビルド済みの Docker コンテナが用意されています。

使い始めるには、次のように入力するだけです。

```
$ source spresense_env.sh
```

このスクリプトは `spresense` というエイリアスを作成します。通常の SDK のビルドスクリプトや Make コマンドの前に、このエイリアスを付けて実行します。

例:
```
SpresenseSDK: $ spresense tools/config.py examples/hello
SpresenseSDK: $ spresense make
```

[twitter spresense handle]: https://img.shields.io/twitter/follow/SpresensebySony?style=social&logo=twitter
[twitter spresense badge]: https://twitter.com/intent/follow?screen_name=SpresensebySony
[twitter devworld handle]: https://img.shields.io/twitter/follow/SonyDevWorld?style=social&logo=twitter
[twitter devworld badge]: https://twitter.com/intent/follow?screen_name=SonyDevWorld
