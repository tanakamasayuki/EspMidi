# Peer Tests

[English](README.md)

ESP32-S3 2台を使う自動テストです。

`EspMidi` では core の正しさは [`../unit/`](../unit/) を主戦場にします。peer test はポート(USB / BLE / UART)の境界を確認する smoke test に限定します。

1 台で確認できるものは [`../loopback/`](../loopback/) に置きます(常時接続のボードを占有しないため)。

## ハードウェア

常時接続された ESP32-S3 2台を使います。

- Host board: USB Host / BLE Host 側のポートを実行する ESP32-S3。
- Device board: USB Device / BLE Device 側のポートを実行する ESP32-S3。

USB のテストでは USB データ線を直結します(BLE のテストは無線なので配線不要ですが、同じ 2 台を使います)。

| Host board | Device board |
| --- | --- |
| GPIO19 (D-) | GPIO19 (D-) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

両方のボードを PC などから別々に給電している場合、VBUS は接続しません。

### UART は同じ配線をクロスとして使う

上の配線はストレートですが、**役割ごとに TX / RX の割り当てを逆にすればクロス結線になります**。追加配線は不要です。

| | Host 役 | Device 役 |
| --- | --- | --- |
| GPIO19 | TX | RX |
| GPIO20 | RX | TX |

31250 baud なので USB の直列抵抗は問題になりません。条件は、そのプロファイルで native USB を使わないこと(コンソールは UART0 = 外付け USB-serial チップ)です。

## 実行

`sketch.yaml` の default profile を使うため、通常は `--profile` を指定しません。

```sh
uv run --env-file .env pytest peer/
```

`.env` では `EspUsbDevice` など既存プロジェクトと同じ変数名を使います。

```sh
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyUSB0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB1
```

開発中の兄弟ライブラリに対して確認する場合は `*_local` プロファイルを指定します。

## 対称性

`EspMidi` は ESP32KeyBridge より peer テストが素直に組めます。**USB Device MIDI ↔ USB Host MIDI**、**BLE MIDI Device ↔ BLE MIDI Host**、**UART ↔ UART** の 3 組がいずれも対称なので、送信役と観測役を入れ替えるだけで双方向を確認できます。

BLE のテストは、前回実行のペアリング状態が結果を変えないよう、開始時と終了時に両側の bond を消します。また 1 ファイル 1 test function にします — BLE リンクは状態を持ち確立にコストがかかるため、1 回ペアリングして途中で assert していくほうが決定的になります。

## 追加済み

- `uart_midi`: 既存配線をクロスとして使い、UART ポートの双方向を確認する(Phase 5)。ノート / CC / SysEx を両向きに通します。1 台で済む往復は [`../loopback/uart_midi/`](../loopback/uart_midi/) にあり、こちらが足すのは **2 つ目のクロック**です(各ボードが、自分が作っていない信号のビットタイミングを解いています)。
- `usb_midi`: USB Device ポートの境界(Phase 6)。**cable 数そのものを `getMidiPortInfo()` で assert**してから、cable を跨いだ往復と SysEx を確認します。DUT 側は素の `EspUsbHost` です — 両端が同じコードでパケットを組むと、cable ニブルの間違いが打ち消し合うためです。
- `ble_midi`: BLE ポートの境界(Phase 8)。DUT が Host、peer が Device で、**無線リンク**・サービスの発見・**タイムスタンプ**(`unit=1` が `Milliseconds13`)・ダンプの往復を確認します。**BLE MIDI はペアリングしない**ので bond は作られません。1 ファイル 1 test function です。
- `usb_midi_host`: USB Host ポートの境界(Phase 7)。両端が `EspMidi` で、こちらが足すのは**動的な側**です。DUT は機器をどこにも書かず、挿されたときに現れた席を `InGroup::all()` のルートが拾います。2 台が報告するポート数は鏡像になります(host は反転せず device は反転する)。

## 追加予定

まだありません。同梱ポートの peer テストは揃いました。
