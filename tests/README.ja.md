# テスト

`EspMidi` のテスト仕様と自動テストを置きます。

ハードウェア非依存部分(共通表現、ポートモデル、ルーティング、フィルタ、変換)は `unit/` で固定します。ポートは build-only、1 台の loopback、2 台の peer、manual に分けて追加します。

## 必要なもの

- `uv`
- `g++`(`unit/` が使う)
- Arduino CLI(`arduino_smoke/` と `examples_compile/` が使う)
- 対象ボード用の ESP32 board package
- 常時接続の ESP32-S3 peer 2台(`peer/`)
- 必要時に接続する loopback / manual 用の ESP32-S3 と ESP32-P4

## 構成

- `unit/`: core のテスト。subject ごとにディレクトリを分け、`g++` でホスト向けにコンパイルして実行する。
- `arduino_smoke/`: 同じヘッダを **Arduino ライブラリとして** ビルドできることの確認。
- `examples_compile/`: examples sketch の build-only smoke test。
- `loopback/`: **1 台**で複数のポートを同時に動かし、`EspMidi` 経由で往復させる自動テスト。
- `peer/`: 常時接続された ESP32-S3 **2台**でポートの境界を確認する自動テスト。
- `manual/`: **常時つながっていない機材が要るテスト。**手で実行する。治具を組めば自動で流れる**手動テスト**と、人の判断が本質の**手順書**の 2 種類。

## 実行

```sh
uv run pytest unit/                              # 数秒。arduino-cli 不要
uv run pytest arduino_smoke/
uv run pytest examples_compile/
uv run --env-file .env pytest loopback/
uv run --env-file .env pytest peer/

# 手動テストは治具をつないだうえで、ファイルを明示的に指定する
uv run --env-file .env pytest manual/usb_if_din/usb_if_din.py
```

**手動テストのファイル名には `test_` が付いていません。** そのため `pytest` や `pytest manual/` では collect されず、指定したときだけ動きます。

CI(`.github/workflows/tests.yml`)が回すのは `unit/` `arduino_smoke/` `examples_compile/` の 3 つで、上と同じコマンドを使います。実機を要する `loopback/` `peer/` `manual/` は CI 対象外です。

examples compile は通常全 example を確認します。開発中に対象を絞る場合は `ESPMIDI_EXAMPLES` に example 名を comma 区切りで指定します。

```sh
ESPMIDI_EXAMPLES=UartMonitor uv run pytest examples_compile/
```

## unit と arduino_smoke を分けている理由

`unit/` は `g++` を直接呼びます。core は Arduino / ESP-IDF / ハードウェアに依存しない純粋 C++ なので([../docs/CORE_DESIGN.ja.md](../docs/CORE_DESIGN.ja.md))、**仕様を固定するテストが arduino-cli も board package も要求せず数秒で終わります**。しかも Arduino core が include パスに無い状態でコンパイルするので、core がうっかり Arduino に依存し始めた瞬間に落ちます。

ただしそれだけでは「Arduino ライブラリとして解決できるか」(library.properties の配置、include パス、Arduino core との同居)は確認できません。そこを `arduino_smoke/` が小さく担保します。仕様の主戦場は `unit/` なので、`arduino_smoke/` は意図的に小さく保ちます。

## 兄弟ライブラリの released 版と local 版

`EspMidi` は `EspUsbHost` / `EspUsbDevice` / `EspBle` に依存し、そのうち 2 つには変更依頼を出しています([../docs/LIBRARY_REQUESTS.ja.md](../docs/LIBRARY_REQUESTS.ja.md))。開発中の基盤ライブラリと `EspMidi` を同時に進められるように、実機プロファイルにはリリース版と local ディレクトリ版を並べます。

```yaml
profiles:
  s3_peer_host:          # リリース版に対して確認する(既定)
    libraries:
      - dir: ../../../
      - EspUsbHost (2.7.3)

  s3_peer_host_local:    # 開発中の兄弟ライブラリに対して確認する
    libraries:
      - dir: ../../../
      - dir: ../../../../EspUsbHost
```

`EspUsbDevice` の `tests/loopback/` と同じ形です。

## 環境変数

`.env.example` を `.env` へコピーして、接続しているボードのシリアルポートを書きます。変数名は sketch.yaml の profile 名から pytest-embedded が導出する形に合わせます。

現在のカバレッジと追加予定は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) を参照してください。
