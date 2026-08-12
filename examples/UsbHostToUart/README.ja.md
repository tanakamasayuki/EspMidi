# UsbHostToUart

[English](README.md)

USB MIDI キーボードで MIDI DIN の音源を鳴らしながら、**同じ演奏を PC にも流します**。PC で録りながら音源を弾けます。

[../../docs/USE_CASES.ja.md](../../docs/USE_CASES.ja.md) の UC1 です。互いを知らない 3 つのトランスポートをルートだけで繋ぐ、このライブラリの要点がそのまま出ています。

## 構成

```text
USB MIDI キーボード ──→ ┐
                        ├──→ MIDI DIN OUT(音源)
                        └──→ PC(USB Device ポート 1)
PC(ポート 1)────────────→ MIDI DIN OUT にも
```

USB Host と USB Device を同時に使うので **USB ペリフェラルが 2 つ必要**です。対象は ESP32-P4 です。

| 定数 | 既定 | 意味 |
| --- | --- | --- |
| `RX_PIN` | 20 | MIDI IN |
| `TX_PIN` | 19 | MIDI OUT(音源へ) |

## 読みどころ

**キーボードをどこにも書いていません。** 席は挿したときに現れるので、ルートは `espmidi::InGroup::all()` に対して張ります。抜き差ししてもルートは消えず、挿し直せばそのまま続きます。

**ルートは来た元のエンドポイントへは返しません。** だから「全入力を音源と PC へ」と書いても、PC が自分にエコーしたり DIN 入力が自分の出力へ回ったりしません。

**席の出入りは通知で分かります。** `registry.addListener()` が拾うのは「席が現れた」と「席の状態が変わった」の 2 つだけです。席は削除されないので、それで足りています。

**cable 数の向きは Host と Device で逆です。** `EspUsbHost` の数は既にホスト視点なので USB Host ポートでは反転せず、USB Device ポートでは反転します。詳しくは [../../docs/PORTS.ja.md](../../docs/PORTS.ja.md)。
