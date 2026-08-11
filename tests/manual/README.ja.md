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

まだありません。[../TEST_PLAN.ja.md](../TEST_PLAN.ja.md) のカバレッジ計画を参照してください。

## 追加予定

- `uart_midi_din`: 実 MIDI DIN(フォトカプラ・220Ω・5V カレントループ)経由で外部音源へ送る。自動テストは UART バイト層までなので、物理層はここで確認する(Phase 5)。
- `usb_device_host_os`: PC / Mac が MIDI インターフェースとして認識し、DAW のポート一覧に **cable 数ぶんのポートが名前付きで並ぶ**ことを確認する(Phase 6)。
- `usb_host_real_devices`: 実際の USB MIDI キーボード・パッド・音源をつないで動作を確認する。複数機器、USB ハブ経由、複合機器を含む(Phase 7)。
- `ble_midi_pairing`: 実 BLE MIDI 機器とのペアリングと再接続を確認する。iOS / Android / PC からの見え方も含む(Phase 8)。
- `sysex_dump`: 実機の音色ダンプ(長い SysEx)を実際に転送し、受け側で正しく読み込めることを確認する(Phase 5 以降)。
- `control_mapping`: 実ボタン・実エンコーダの操作感とサンプリング精度を確認する(Phase 9)。
