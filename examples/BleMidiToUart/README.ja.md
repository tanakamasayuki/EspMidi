# BleMidiToUart

[English](README.md)

**ワイヤレスの BLE MIDI キーボードで、MIDI DIN の音源を鳴らします。** PC は要りません。

[../../docs/USE_CASES.ja.md](../../docs/USE_CASES.ja.md) の UC5 です。逆向きにも流れるので、MIDI DIN 入力に来たものはキーボードへ送られます(ランプや画面を持つコントローラを光らせられます)。

## 構成

```text
BLE MIDI キーボード ──→ MIDI DIN OUT(音源)
MIDI DIN IN ────────→ BLE MIDI キーボード
```

| 定数 | 既定 | 意味 |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT(音源へ) |

## 読みどころ

**スキャンと接続はスケッチの仕事、接続の中身はポートの仕事です。** だから `discover()` はどこにも書いていません。BLE は `EspMidi` の担当外です([../../docs/CORE_DESIGN.ja.md](../../docs/CORE_DESIGN.ja.md))。

**キーボードをどこにも書いていません。** ルートは 2 本ともポート群に対して張るので、抜き差ししても張り直しません。

```cpp
router.addRoute(espmidi::InGroup::all(), din.out());   // 鳴らすものは音源へ
router.addRoute(din.in(), espmidi::OutGroup::all());   // DIN 入力は繋がっているものへ
```

**ルートは来た元のエンドポイントへは返しません。** だから 2 本目は音源の出力へ回らず、キーボードにだけ届きます。

**再接続すると同じ席に戻ります。** BLE アドレスが識別子なので、圏外に出て戻ってきてもルートは生きたままです。

**タイムスタンプが付いてくる唯一のポートです。** 13 bit ミリ秒(`TimestampUnit::Milliseconds13`)で、このライブラリは中身を解釈しません。
