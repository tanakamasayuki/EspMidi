# テスト計画

## テスト方針

`EspMidi` は入力ポート、共通表現、統合・変換、出力ポートを分離するライブラリです。テストも同じ分け方にします。

**unit test** は、共通表現とポートモデル、ルーティング、フィルタ、変換、そしてポートそのものをホスト上で検証します。ハードウェアが不要な仕様はここで固定します。`g++` を直接呼ぶので arduino-cli も board package も要りません。

**arduino_smoke** は、同じヘッダが Arduino ライブラリとしてビルドできることを確認します。仕様の検証ではなく経路の検証です。

**build-only test** は、examples が対象ボード向けにコンパイルできることを確認します。

**loopback test** は、**1 台**で複数のポートを同時に動かし、`EspMidi` 経由で往復させます。`EspMidi` は複数ポートを 1 つのシステムとして扱うライブラリなので、この構成が最も本質に近いテストです。`loopback/usb_host_device` は**実 USB ケーブルの両端が `EspMidi` のポート**で、同じ router の中にいます。

**peer test** は、常時接続された ESP32-S3 **2台**を使い、ポートの境界だけを確認します。core の正しさは peer ではなく unit test を主戦場にします。

**manual test** は、配線、Host OS の認識、Bluetooth ペアリング、実 MIDI 機器、目視確認が検証の本質に含まれる場合だけに使います。

```text
tests/
  unit/              自動 - 共通表現・ポートモデル・ルーティング・フィルタ・変換
  arduino_smoke/     自動 - Arduino ライブラリとしての build 経路
  examples_compile/  自動 - examples sketch の build-only smoke
  loopback/          自動 - 1 台で複数ポートを往復
  peer/              自動 - 2 台でポート境界を確認
  manual/            手動 - OS 認識、ペアリング、配線、実 MIDI 機器、目視
```

## テスト環境

通常の自動テスト環境は、ローカル PC と常時接続された ESP32-S3 peer 2台を前提にします。

- **Local**: unit test、arduino_smoke、examples compile。unit は `g++` だけで回る。
- **S3 loopback**: 必要時に接続する ESP32-S3 1台。UART MIDI の往復に使う。`.env` では `TEST_SERIAL_PORT_S3_LOOPBACK`。
- **S3 peer**: 常時接続された ESP32-S3 2台。USB data pin を直結し、USB / BLE / UART のポート境界を確認する。`.env` では `TEST_SERIAL_PORT_S3_PEER_HOST` と `TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE`。
- **P4 loopback**: 必要時に接続する ESP32-P4 1台。USB Host と USB Device を同一ボードで動かす構成に使う。常時接続の前提にはしない。
- **Manual S3**: 実 MIDI 機器、実 MIDI DIN 配線、DAW の認識確認など。`.env` では `TEST_SERIAL_PORT_ESP32S3`。

## UART の配線

**UART の自動テストに追加配線は不要です。**

| テスト | 配線 | 方法 |
| --- | --- | --- |
| `loopback/uart_midi` | **ゼロ** | GPIO マトリクスで UART1_TX と UART2_RX を同一 GPIO に割り当てる。実ペリフェラル 2 つを通る |
| `peer/uart_midi` | **既存のまま** | USB peer の配線(GPIO19↔19 / GPIO20↔20 / GND、ストレート)を、役割ごとに TX / RX を入れ替えてクロスとして使う |
| `manual/uart_midi_din` | 要配線 | フォトカプラ・220Ω・5V の実 MIDI DIN |

peer のクロスの内訳は次のとおりです。31250 baud なので USB の直列抵抗は問題になりません。条件は、そのプロファイルで native USB を使わないこと(コンソールは UART0 = 外付け USB-serial チップ)です。

| | Host 役ボード | Device 役ボード |
| --- | --- | --- |
| GPIO19 | TX | RX |
| GPIO20 | RX | TX |

自動テストが対象とするのは **UART バイト層**です。実 MIDI DIN の 5V カレントループとフォトカプラは `manual/` に隔離します。

**配線ゼロの構成には 1 箇所だけ手当てが要ります。** Arduino のピン管理は 1 ピンにつき 1 ペリフェラルしか覚えず、2 つ目が同じピンを要求すると 1 つ目を外します。そこで受信側を先に `begin()` してから、送信信号を `esp_rom_gpio_connect_out_signal()` でピンに重ねます。出力信号の接続は同時にピンの出力ドライバも有効にし、受信側が張った入力経路はそのまま残るので、これで 1 本のピンを 2 つのペリフェラルが共有します。

## カバレッジ計画

実装順([../docs/DEVELOPMENT_PLAN.ja.md](../docs/DEVELOPMENT_PLAN.ja.md))に沿って積みます。

| Phase | 対象 | unit | arduino_smoke | examples_compile | loopback | peer | manual |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | リポジトリ構造 / 公開ヘッダのホストビルド | 実装済み | 実装済み | | | | |
| 1 | 共通表現 / MIDI 1.0 パーサ / SysEx チャンク / USB パケットコーデック | 実装済み | | | | | |
| 2 | ポートモデル(席 / 状態 / メタデータ / ポート群) | 実装済み | | | | | |
| 3 | ルーティング / 3 段パイプライン / キュー / SysEx 3 規則 / アプリケーションポート | 実装済み | | | | | |
| 4 | フィルタ / 変換 | 実装済み | | | | | |
| 5 | UART ポート | 実装済み | | 実装済み | **実機で確認**(配線ゼロ) | **実機で確認**(既存配線をクロス) | 手順あり |
| 6 | USB Device ポート(複数 cable) | 実装済み | | 実装済み | **実機で確認**(P4) | **実機で確認** | 手順あり |
| 7 | USB Host ポート(動的接続 / 複数機器) | 実装済み | | 実装済み | **実機で確認**(P4) | **実機で確認** | 手順あり |
| 8 | BLE ポート(Device / Host) | 実装済み | | 実装済み | | **実機で確認** | 手順あり |
| 9 | Control Mapping ヘルパー | 実装済み | | 実装済み | | | **手順あり** |

## peer テストの位置付け

peer テストは、ポートが実 USB / BLE / UART 経由で期待どおりメッセージを受け渡せることを確認する smoke test に限定します。ルーティング、フィルタ、変換の正しさは peer ではなく unit test で固定します。

`EspMidi` は KeyBridge より peer テストが素直に組めます。**USB Device MIDI ↔ USB Host MIDI**、**BLE MIDI Device ↔ BLE MIDI Host**、**UART ↔ UART** の 3 組がいずれも対称なので、送信役と観測役を入れ替えるだけで双方向を確認できます。

BLE のテストは配線不要(無線リンク)ですが、使うボードは同じ 2 台です。**BLE MIDI はペアリングを必要としない**ので `peer/ble_midi` は bond を作らず、したがって消す必要もありません。将来ペアリングを要求するテストを足す場合は、前回実行の状態が結果を変えないよう開始時と終了時に両側の bond を消します。

## cable のテストで気をつけること

**往復テストは cable の申告を証明しません。** 受信メッセージの cable 番号はパケットヘッダの上位ニブルをそのまま読んだ値なので、**1 cable しか宣言していない descriptor でも cable=3 として往復してしまいます**。これは `EspUsbDevice` 側の実装時に判明した落とし穴で、同じ罠が `EspMidi` の peer テストにもあります。

したがって cable のテストは 2 段構えにします。

1. **descriptor / ポート数そのものを検証する。** `EspMidi` の USB Device ポートが宣言した cable 数を、Host 役が `EspUsbHost::getMidiPortInfo()` の `inCableCount` / `outCableCount` で読んで assert します。**device が実際に cable を申告したことを実機で示せるのはこれだけです。**
2. **そのうえで cable を跨いだ往復を確認する。** 1 を通さずに 2 だけ書くと、ポートが 1 本しかなくてもテストが通ってしまいます。

`getMidiPortInfo()` は **EspUsbHost 2.7.5** で、複数 cable の `EspUsbDeviceMidi` は **EspUsbDevice 2.0.2** でリリース済みです。したがって peer テストは公開バージョンに対して回します。決定 5 のとおり `*_local` プロファイルも並べてあり、兄弟ライブラリの開発版に対して確認したいときだけ `--profile=s3_peer_local` で選びます。

なお `EspUsbHostMidiMessage::cable` はリリース済みなので、cable を跨いだ往復だけならリリース版でも確認できます。

## 実機テストのスケッチの書き方

**`setup()` で結果を出さないでください。** 書き込みツールがボードをリセットし、コンソールが開かれるのはその後なので、起動直後の出力は誰も聞いていないうちに終わります。空のログだけが残ります。

- 準備完了は**繰り返し**告げる
- 本番はホストに促されてから走らせる。**促しは「決めた 1 文字」で判定する** — 書き込みツールが線を離すときに紛れ込む 1 バイトで走り出すと、コンソールが開く前に全部終わってしまう(ESP32-P4 で実際に起きた)
- こうすると、ボードがいつ起動したかにテストが依存しなくなる

`loopback/uart_midi` と `peer/uart_midi` はこの形です。

## manual に残したもの

自動テストで assert できないものだけを [`manual/`](manual/) に手順として置いてあります。**6 件すべて手順が書かれていて、リリース前に一度は人が通す必要があります。**

| 手順 | ここでしか分からないこと |
| --- | --- |
| `uart_midi_din` | 5V カレントループとフォトカプラ、絶縁 |
| `usb_device_host_os` | OS のポート一覧での名前の出方、DAW からの見え方 |
| `usb_host_real_devices` | 実機の descriptor の癖、シリアルを持たない機器 |
| `ble_midi_pairing` | ペアリング操作、ランダムアドレスの機器、体感レイテンシ |
| `sysex_dump` | 数 KB のダンプ、送信中の切断、BLE の上限 |
| `control_mapping` | 人の手に対する操作感 |

## 追加時の判断基準

- 仕様は、できるだけ `unit/` で先に固定する。
- ポートは、まず build-only test を追加して API の破壊を検出する。
- 実機の自動テストは、シリアルログだけで期待値を assert できる範囲に限定する。
- 人の判断が必要な確認は `manual/` に隔離し、自動テストの合格条件に混ぜない。
- 1 台で確認できるものは `peer/` ではなく `loopback/` に置く(常時接続のボードを占有しないため)。
