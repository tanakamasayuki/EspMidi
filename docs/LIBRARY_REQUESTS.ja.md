# 基盤ライブラリへの変更依頼台帳

`EspMidi` は既存の汎用ライブラリが提供する MIDI 機能の上に統合層を提供する。基盤ライブラリへ変更が必要になった場合、その依頼と状況をここで追跡する。

依頼の本文は**依頼先リポジトリの `docs/` に置く**。同一作者の管理下にあるライブラリ群で、既に [`EspUsbHost/docs/lifecycle-listener-proposal.ja.md`](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/lifecycle-listener-proposal.ja.md) という先例がある形式に合わせている。

## 方針

依頼を出す前に、`EspMidi` 側で回避できないかを確認する。回避処理で済ませられるものは依頼しない。依頼するのは次のいずれかに該当する場合だけとする。

- 基盤ライブラリしか持っていない情報が公開されていない
- 基盤ライブラリしか組み立てられない descriptor / プロトコル要素が固定されている
- `EspMidi` 側で実装すると同じワイヤ形式の実装が二重になる

`EspMidi` は基盤ライブラリの **RAW 層** だけを使う。各ライブラリの MIDI 便利ラッパーと example は、`EspMidi` を使わない利用者のために残す(`DECISIONS.ja.md` の決定 2)。したがって依頼はすべて「RAW 層の能力不足」に限られる。

## 一覧

| # | 依頼先 | 内容 | 破壊的 | 状態 | 必要になる Phase |
| --- | --- | --- | --- | --- | --- |
| 1 | EspUsbDevice | MIDI 複数 cable 対応(descriptor 生成)+ cable 名 | いいえ | **cable 数は実装済み** / cable 名は見送り | 6 |
| 2 | EspUsbHost | 接続機器の MIDI cable 数と jack 名の公開 | いいえ | **cable 数は実装済み** / cable 名は未着手 | 7 |

### 1. EspUsbDevice — MIDI 複数 cable 対応

本文: [`EspUsbDevice/docs/MIDI_MULTI_CABLE_PROPOSAL.ja.md`](https://github.com/tanakamasayuki/EspUsbDevice/blob/main/docs/MIDI_MULTI_CABLE_PROPOSAL.ja.md)

`configurationDescriptor()` が TinyUSB の 1 cable 固定テンプレート `TUD_MIDI_DESCRIPTOR` を使っているため、ESP32 を複数ポートの MIDI インターフェースとして PC に見せられなかった。パケット API 側は `header` の上位ニブルで既に cable を扱えており、必要な TinyUSB マクロも cable 数で parameterize されているので、**descriptor の組み立てだけの変更**で済んだ。

**cable 数は実装済み。** `EspMidi` 側が使う API は次のとおり。

```cpp
static constexpr uint8_t MAX_CABLES = 16;   // EspMidi の 1 エンドポイント上限と一致
explicit EspUsbDeviceMidi(EspUsbDevice &device, uint8_t cableCount = 1);   // 1..16 にクランプ
EspUsbDeviceMidi(EspUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount);
uint8_t  inCableCount() const;   // device → host(この機器が送る側)
uint8_t  outCableCount() const;  // host → device(受ける側)
uint16_t descriptorLength() const;
// helper は 0 起算の cable 引数を取る。範囲外は false を返す
bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable = 0);
```

**方向ごとに cable 数を変えられます。** 方向の呼び方は USB のエンドポイント方向と `EspUsbHostMidiPortInfo` に揃えてホスト視点で統一されているので、`EspMidi` は両者を同じ意味で扱えます(MIDI クラス仕様が embedded jack を機器側視点で呼ぶ点だけが例外)。

helper が `cableCount()` 以上の cable で **false を返す**のは重要な性質で、「Host が知らないポートに載るパケットを黙って出す」ことがありません。`EspMidi` は `readPacket()` / `writePacket()` の生パケットを使うので helper は通りませんが、cable 範囲外を拒否する方針は `EspMidi` 側でも揃えます。

**提案の誤りが 2 つ訂正された。**

1. 「1 cable では 88 バイト」は誤りで、実際は **92 バイト**。`TUD_MIDI_DESC_EP_LEN(1)` はマクロ外で出力される jack ID の 1 バイトを含むので 14 で、34 + 30 + 14 × 2 = 92。TinyUSB 側の `// Length of template descriptor (88 bytes)` というコメント自体が stale だった。16 cable = 572 バイトは正しい。
2. 「バッファが収容できるか確認が必要」は弱すぎた。`MAX_CONFIG_DESCRIPTOR` は 256 バイトで**明確に不足**しており、しかも 1 device あたり 3 本ある。704 バイトへ拡張された(RAM +1344 バイト)。

**提案が見落としていた点。** これは「足りない」ではなく **サイレントにオーバーランする**問題だった。呼び出し側は `configurationDescriptorForSpeed()` に容量を渡していたが、基底のデフォルト実装がその引数を捨てて `configurationDescriptor()` へ転送していた。MIDI 側で `configurationDescriptorForSpeed()` を override して事前に容量を検査し、収まらなければ 0 を返す形になった。

**cable 名は見送り。** `stringDescriptor()` は index 1/2/3 のハードコードな if 連鎖で、文字列テーブルも index アロケータも無い(`index 4` は Net と衝突する)。加えて **`TUD_MIDI_DESC_JACK_DESC` は 1 つの string index を 4 つの jack すべてに付けるため、jack 単位の名前指定はそもそも不可能**。cable 単位の名前なら原理的には可能だが、文字列テーブルの新規実装が前提になるため、ポート名は Host 側の命名に任せる。

### 2. EspUsbHost — MIDI cable 数と jack 名の公開

本文: [`EspUsbHost/docs/midi-cable-discovery-proposal.ja.md`](https://github.com/tanakamasayuki/EspUsbHost/blob/main/docs/midi-cable-discovery-proposal.ja.md)

MIDI descriptor の jack 情報を全くパースしていないため、接続機器のポート数が分からなかった。`EspMidi` のポートは「論理的な席」で、接続時点にポート数が確定していることを前提にしている([DATA_MODEL.ja.md](DATA_MODEL.ja.md) のポートモデル)。特に**ホスト → 機器方向の cable 数を知る手段が全く無い**のが問題で、複数ポートを持つ MIDI 音源へ振り分ける構成が組めなかった。

**cable 数は実装済み。** ただし提案からシグネチャが変わっている。MIDI API 全体が末尾に既定値付きの `address` を取る規約に合わせたためで、`EspMidi` 側はこちらを使う。

```cpp
bool getMidiPortInfo(EspUsbHostMidiPortInfo &info,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

**cable 名は未着手。** ESP-IDF の USB Host は文字列を manufacturer / product / serial の 3 本しかキャッシュせず、任意の string index を取る API が無いため、`iJack` には GET_DESCRIPTOR(STRING) の制御転送を自前で実装する必要がある。cable 数(descriptor のパースのみ、I/O なし)とはコストが桁違いなので分離された。実装するときは `bool input` 引数をやめ、`midiInCableName()` / `midiOutCableName()` の 2 関数に分ける方針。

**`EspMidi` 側が守るべき注意点。**

- **jack の方向は endpoint の方向と反転する。** クラス仕様は embedded jack を機器側から見た名前で呼ぶので、bulk **IN** endpoint の CS_ENDPOINT が列挙するのは Embedded MIDI **OUT** Jack である。`inCableCount` / `outCableCount` はホストから見た方向に統一されているのでそのまま使えるが、descriptor を直接読む場面では反転を忘れないこと。
- **追跡されるのは最初の MIDI Streaming インターフェースと、方向ごとに 1 本の bulk endpoint だけ。** cable 番号は endpoint ごとに 0 起算なので、同一方向の MS bulk endpoint を複数持つ機器は表現できない。これは `midiSend()` と受信コールバックの対象範囲と同じ制約なので、`EspMidi` のエンドポイントは「1 機器 = 1 エンドポイント」で対応が付く。
- **cable 数 0 は「その方向が無い」以外の意味も持つ。** descriptor が MS_GENERAL でない、宣言した jack ID を収めるには短い、cable 番号(4 bit)で指せない本数を宣言している場合も 0 になる。ポートを 1 本と決め打ちせず、0 なら「その方向のポートを作らない」とする。

## 依頼不要と確認済みのもの

同じ確認を繰り返さないための記録。

| 必要なもの | 結論 |
| --- | --- |
| EspUsbHost の接続 / 切断イベント | `addDeviceConnectedListener()` / `addDeviceDisconnectedListener()` で足りる |
| EspUsbHost の機器識別(席の再照合) | `EspUsbHostDeviceInfo` の `vid` / `pid` / `serial` / `product` で足りる |
| EspUsbHost の cable 指定送信 | `midiSend()` が生バイト送信なので `EspMidi` が header に cable を立てられる |
| EspUsbDevice の受信コールバック | 不要。`readPacket()` のポーリングは `EspMidi` の `update()` 駆動と相性が良い |
| EspUsbDevice の SysEx 送受信 | 不要。生パケットなので CIN 0x4〜0x7 を `EspMidi` が組める |
| EspUsbDevice の `g_activeMidi` シングルトン解消 | 不要。1 Endpoint = 1 インスタンス(最大 16 cable)で要件を満たすため、複数インスタンスは要らない |
| EspBle の接続 / 切断イベント | `addConnectedListener()` / `addDisconnectedListener()` で足りる |
| EspBle の MIDI コーデック | 不要。`EspBleMidiDevice` / `EspBleMidiHost` に乗る方針(決定 2)なので、`EspMidi` 側に BLE ワイヤ形式を持たない |
