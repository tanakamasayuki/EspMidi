# SameCodeAnyPort

[English](README.md)

**同じ MIDI コードを UART・USB・BLE で動かします。** 変えるのは 1 行だけです。

```cpp
#define MIDI_PORT MIDI_PORT_UART   // MIDI_PORT_USB / MIDI_PORT_BLE
```

**ポートが 1 つだけでもこのライブラリを使う理由がこれです。**

## 何をするスケッチか

受け取ったノートを **1 オクターブ上げて返します**。届いたものはコンソールにも出ます。エコー自体は本題ではありません。

## 読みどころ

`#if` から下は**インターフェースを変えても 1 文字も変わりません**。

```cpp
espmidi::Filter notesOnly;
notesOnly.kinds = espmidi::KindNotes;
router.setRouteFilter(echo, notesOnly);

espmidi::Transform octaveUp;
octaveUp.transpose = 12;
router.setRouteTransform(echo, octaveUp);
```

**トランスポートごとに違うのはスタックの起動だけです。**

| | UART | USB Device | BLE Device |
| --- | --- | --- | --- |
| スタック起動 | 不要(ポートが serial を開く) | `usb.begin(config)` | `bleMidi.begin()` → `ble.begin()` → advertising |
| `loop()` で回すもの | — | `usb.task()` | `ble.update()` |
| **ルート・フィルタ・変換** | **同じ** | **同じ** | **同じ** |

だから

- UART で作って、あとで USB MIDI にする → ポートの宣言と `begin()` だけ
- USB で作って、BLE も足す → ポートを増やしてルートを 1 本足す
- 「SysEx はどう届く?」「velocity 0 は?」→ **どのトランスポートでも同じ答え**

トランスポートごとの API を直接使うと、この差分が**アプリ側に散ります**。

**このエコーは意図的に来た元へ返しています。** ルートは既定で来た元のエンドポイントへ返さないので、`setRouteAllowSameEndpoint(echo, true)` を明示しています。MIDI Thru を作るときと同じ形です。**相手も同じことをしていると無限ループになる**ので注意してください([../../docs/MIDI_BASICS.ja.md](../../docs/MIDI_BASICS.ja.md))。

## プロファイル

3 つのトランスポートすべてが `sketch.yaml` に入っているので、`MIDI_PORT` をどれにしてもビルドできます。**実際のスケッチでは使うものだけで十分**です。
