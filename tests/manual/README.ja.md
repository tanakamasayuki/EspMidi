# Manual Tests

[English](README.md)

**常時つながっていない機材が要るテスト**を置きます。手で実行します。

置いてあるものは 2 種類です。

| | |
| --- | --- |
| **手動テスト** | 治具を組むまでが手動で、**そのあとは自動で流れる**。`pytest` に**明示的にファイルを指定**して実行する |
| **手順書** | 人の操作や判断そのものが検証対象で、assert できない |

**どちらも自動テストの合格条件には混ぜません。** 常時接続の環境で assert できるものは `unit/` `loopback/` `peer/` のいずれかに置きます。

## manual に置く基準

次のいずれかに該当するものだけを置きます。

- **常時接続していない機材が要る**(実 MIDI DIN 回路、USB MIDI インターフェース、実機など)
- 実配線が必要(実 MIDI DIN のフォトカプラ回路など)
- Host OS や DAW の認識が検証対象(ポート名の表示、ポート数の見え方)
- Bluetooth のペアリング操作が検証対象
- 実 MIDI 機器の挙動が検証対象(機器ごとの SysEx の癖など)
- 目視・聴感での確認が必要(実際に音が鳴るか、レイテンシが実用的か)

## 手動テスト

**ファイル名から `test_` を外してあります。** そのため `pytest` や `pytest manual/` では collect されず、**指定したときだけ**動きます。常時つながっていない機材が前提なので、うっかり自動実行されないようにするためです。

- [`usb_if_din/`](usb_if_din/README.ja.md): **USB MIDI インターフェースと MIDI DIN 回路を 1 本の輪に**して、UART ポートと USB Host ポートを相互に通します。往路と復路が別の物理層です。GPIO は環境変数から渡します。

```sh
uv run --env-file .env pytest manual/usb_if_din/usb_if_din.py
```

## 手順書

- [`uart_midi_din.ja.md`](uart_midi_din.ja.md): 実 MIDI DIN の物理層(フォトカプラ・220Ω・5V カレントループ)。自動テストは UART バイト層までです。
- [`usb_device_host_os.ja.md`](usb_device_host_os.ja.md): PC / Mac / DAW からどう見えるか。ポート数は `peer/usb_midi` が assert していますが、**一覧での名前の出方は OS 次第**です。
- [`usb_host_real_devices.ja.md`](usb_host_real_devices.ja.md): 実際の USB MIDI 機器。複数機器、ハブ経由、複合機器、**シリアルを持たない機器**を含みます。
- [`ble_midi_pairing.ja.md`](ble_midi_pairing.ja.md): 実 BLE MIDI 機器と OS からの見え方。ペアリングと**ランダムアドレスの機器**を含みます。
- [`sysex_dump.ja.md`](sysex_dump.ja.md): 実機の音色ダンプ。数 KB のダンプでキューの分割・出力の排他・送信中の切断が効いてきます。
- [`control_mapping.ja.md`](control_mapping.ja.md): 実ボタン・実つまみ・実エンコーダの操作感。`unit/control_mapping` が固定しているのは値と時刻の扱いで、**人の手に対してどう感じられるかは assert できません**。

## 追加予定

まだありません。手順は揃いました。**どれも一度は人が通す必要があります** — リリース前の確認項目です([../../docs/RELEASE_CHECKLIST.ja.md](../../docs/RELEASE_CHECKLIST.ja.md))。

手動テストのほうは、**治具さえ組めば人の判断が要らない**ので、手順書より先に片付きます。
