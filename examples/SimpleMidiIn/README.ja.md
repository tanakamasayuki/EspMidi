# SimpleMidiIn

[English](README.md)

**1 ポート・受信のみ。** 届いた MIDI をコンソールに表示します。

キーボードの MIDI OUT をつないで鍵盤を弾けば、何が送られているかが見えます。

## 配線

キーボードの MIDI OUT → **MIDI IN 回路(フォトカプラ)** → `RX_PIN`(既定 GPIO20)。

**MIDI DIN ソケットを GPIO に直結してはいけません。** MIDI は 5V のカレントループで、絶縁が必須です。3.3V の GPIO が壊れます([../../docs/MIDI_BASICS.ja.md](../../docs/MIDI_BASICS.ja.md))。

## 読みどころ

**`din.update()` と `router.update()` の 2 つが必要です。** 前者が線を読み、後者がルーティングを走らせてコールバックを呼びます。

**velocity 0 のノートオンはノートオフです。** このスケッチはそれを表示で区別しています。知らないと「音が鳴り止まない」バグになる、MIDI で最も定番の罠です。

**チャンネルは 0 始まりです。** 機器の表示に合わせるなら `channel() + 1` です。

**`message` の中のポインタはコールバックの間だけ有効です。** 後で使うならコピーします。

次は [`../SameCodeAnyPort/`](../SameCodeAnyPort/) です。ガイドは [../../docs/GUIDE.ja.md](../../docs/GUIDE.ja.md)。
