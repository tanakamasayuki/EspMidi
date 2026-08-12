# Manual Tests

[English](README.md)

人の操作や判断が検証の本質に含まれるテストの手順を置きます。

**ここの手順は自動テストの合格条件に混ぜません。** 自動テストで assert できるものは `unit/` `loopback/` `peer/` のいずれかに置きます。

## manual に置く基準

次のいずれかに該当するものだけを置きます。

- 実配線が必要(実 MIDI DIN のフォトカプラ回路など)
- Host OS や DAW の認識が検証対象(ポート名の表示、ポート数の見え方)
- Bluetooth のペアリング操作が検証対象
- 実 MIDI 機器の挙動が検証対象(機器ごとの SysEx の癖など)
- 目視・聴感での確認が必要(実際に音が鳴るか、レイテンシが実用的か)

## 手順

- [`uart_midi_din.ja.md`](uart_midi_din.ja.md): 実 MIDI DIN の物理層(フォトカプラ・220Ω・5V カレントループ)。自動テストは UART バイト層までです。
- [`usb_device_host_os.ja.md`](usb_device_host_os.ja.md): PC / Mac / DAW からどう見えるか。ポート数は `peer/usb_midi` が assert していますが、**一覧での名前の出方は OS 次第**です。
- [`usb_host_real_devices.ja.md`](usb_host_real_devices.ja.md): 実際の USB MIDI 機器。複数機器、ハブ経由、複合機器、**シリアルを持たない機器**を含みます。
- [`ble_midi_pairing.ja.md`](ble_midi_pairing.ja.md): 実 BLE MIDI 機器と OS からの見え方。ペアリングと**ランダムアドレスの機器**を含みます。
- [`sysex_dump.ja.md`](sysex_dump.ja.md): 実機の音色ダンプ。数 KB のダンプでキューの分割・出力の排他・送信中の切断が効いてきます。
- [`control_mapping.ja.md`](control_mapping.ja.md): 実ボタン・実つまみ・実エンコーダの操作感。`unit/control_mapping` が固定しているのは値と時刻の扱いで、**人の手に対してどう感じられるかは assert できません**。

## 追加予定

まだありません。手順は揃いました。**どれも一度は人が通す必要があります** — リリース前の確認項目です([../../docs/RELEASE_CHECKLIST.ja.md](../../docs/RELEASE_CHECKLIST.ja.md))。
