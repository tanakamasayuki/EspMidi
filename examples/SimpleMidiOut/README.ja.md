# SimpleMidiOut

[English](README.md)

**最小のスケッチです。** 1 ポート・送信のみ。ドレミファソラシドを繰り返し鳴らします。

音源を MIDI OUT につないで電源を入れれば音が出ます。最初に動かすならこれです。

## 配線

`TX_PIN`(既定 GPIO19)→ MIDI OUT 回路(220Ω × 2)→ 音源の MIDI IN。

**MIDI IN 側は使いません。** 送信だけなので `begin()` の `rxPin` に `-1` を渡しています。

## 読みどころ

**`router.update()` を呼ぶまで線に出ません。**

```cpp
sketch.sendShort(0x90, 60, 100);  // 積むだけ
router.update();                  // ここで出る
```

**`espmidi::AppPort` は「スケッチというポート」です。** ほかのポートと同じようにルーティングされるので、「PC にも同時に送りたい」と言われても**ルートを 1 本足すだけ**です。

**ノートオフを忘れると鳴り続けます。** MIDI 自体の注意点は [../../docs/MIDI_BASICS.ja.md](../../docs/MIDI_BASICS.ja.md) にまとめてあります。

次は [`../SimpleMidiIn/`](../SimpleMidiIn/)、そのあと [`../SameCodeAnyPort/`](../SameCodeAnyPort/) です。ガイドは [../../docs/GUIDE.ja.md](../../docs/GUIDE.ja.md)。
