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

## 準備と実行

1. [サンプル入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)で導入条件と必要な機器を確認します。対応するボード・外部ライブラリはルートREADMEを参照します。
2. [SameCodeAnyPort.ino](SameCodeAnyPort.ino)を開き、実際のボードとポートを選びます。次のincludeで使う依存機能も準備してください。

   `<EspMidiUart.h>`, `<EspMidiEspUsbDevice.h>`, `<EspMidiEspBle.h>`

3. 書き込んだらシリアルモニタを115200 baudへ合わせます。起動ログとソースの操作条件を確認します。

## 動作の流れ

Transportが異なっても共通のルート処理を使う例です。詳細なAPIとデータの扱いは次の項目で確認します。

以下はソース内で最初に現れるAPIの順です。コールバック登録の位置は、実際の呼び出し順序とは異なります。

1. `port.begin(...)` — 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
2. `ble.advertising().start(...)` — 設定した入口から処理を開始します。実行中はコンテキストとオブジェクトの寿命を保ちます。
3. `ble.update(...)` — 例にある実行場所から定期的に呼び、対象の処理を進めます。
4. `router.addRoute(...)` — 切断と意図しないループを考慮して、入力と出力のEndpoint／グループを登録します。

## APIの使い方と注意点

各Transportとポートを初期化し、ルートを登録してから、例にあるRouter／ポート／スタックの処理を継続します。MIDIを転送するだけでは音は鳴らず、楽器やソフト音源へ接続します。USB／BLEの接続でポートの可用性は変わるため、グループや安定した識別子と、現在のEndpointは別の役割です。

双方向ルートを追加する際は意図しないループを防ぎ、注入したNote OnにNote Offを対応させます。UART MIDIにはDIN用の適切な電気回路が必要で、Socketへ直接GPIOをつなぐ配線ではありません。Filter追加時もPacket長とSysEx viewの寿命を保ちます。

コールバック引数への参照は永続領域ではありません。

## 操作と出力の見方

出力書式の一部は次のとおりです。printfの書式記号は実行時の値に変わります。表示順や数値は実行条件に応じて変わるので、その行へ進む条件と併せて確認します。

```text
in:
ready
```

## 確認と問題の切り分け

Transportが異なっても共通のルート処理を使うことを出力から確認します。まず例のままの入力で試し、その後に設定やデータ源を1つずつ変えます。

| 症状 | 最初に確認すること |
|---|---|
| 出力がない | ボード／依存ライブラリ、初期化結果、表示先とシリアル速度 |
| 開始するが途中で止まる | 待機条件、バッファの空き、相手や入力の準備、継続呼び出し |
| 値が不自然・不安定 | 長さと単位、形式、寿命、共有状態、部分的な処理結果 |

共通の考え方は[入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)、別の例は[サンプル一覧](../README.ja.md)へ進んでください。
