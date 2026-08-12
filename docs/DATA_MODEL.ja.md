# データモデル

`EspMidi` の中間表現とポートモデルの仕様です。この文書は**仕様**を書きます。実装がどこまで進んでいるかは [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) を参照してください。判断の理由と採らなかった案は [DECISIONS.ja.md](DECISIONS.ja.md) にあります。

コード例では namespace(`espmidi::`)を省略しません。

## 中間表現は MIDI 1.0 のバイト列

内部の正準表現は MIDI 1.0 のバイト列です。MIDI 2.0 の Universal MIDI Packet(UMP)を内部表現には採りません。

UMP を採らない理由は長い SysEx です。UMP の SysEx7 は 1 パケット 6 バイト固定なので、10KB の音色ダンプが約 1700 パケットになり、USB → UART の素通し転送でも 6 バイトごとに詰め替えが発生します。「長い SysEx を扱える」は要件([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md))なので、これは実装の都合ではなく要件との衝突です。ポインタ + 長さのチャンクなら、届いた数百バイトをそのまま 1 メッセージとして渡せます。

代わりに、**MIDI 2.0 で行き止まりになる箇所だけを先に外します**。以下の 5 点がそれです。

## メッセージ

```cpp
namespace espmidi {

// UMP の Message Type と同じ番号体系で採番する。現状発生するのは
// System / Midi1ChannelVoice / Data7 の 3 つ。
enum class MessageType : uint8_t {
  Utility           = 0x0,
  System            = 0x1,  // System Common / System Real-Time
  Midi1ChannelVoice = 0x2,
  Data7             = 0x3,  // SysEx7
  // 将来: Midi2ChannelVoice = 0x4, Data128 = 0x5, FlexData = 0xD, Stream = 0xF
};

// 単位を型に持つ。EspMidi は値を解釈しない。
enum class TimestampUnit : uint8_t {
  None,             // 送信元がタイムスタンプを持たない(USB / UART)
  Milliseconds13,   // BLE MIDI の 13 bit ミリ秒(0..8191)
  JrTicks31250,     // 将来: MIDI 2.0 の JR Timestamp(1/31250 秒)
};

struct Timestamp {
  uint16_t      value = 0;
  TimestampUnit unit  = TimestampUnit::None;
};

struct Message {
  PortId      port;       // cable / group を吸収した座標
  MessageType type = MessageType::Midi1ChannelVoice;
  Timestamp   timestamp;

  uint8_t status     = 0; // フルステータスバイト(running status 解決済み)
  uint8_t data1      = 0;
  uint8_t data2      = 0;
  uint8_t dataLength = 0; // 有効なデータバイト数(0..2)

  const uint8_t *raw    = nullptr; // ワイヤのバイト列
  size_t         length = 0;

  // データメッセージのチャンク。SysEx 専用の概念にはしない。
  bool           chunk       = false;
  bool           chunkStart  = false;
  bool           chunkEnd    = false;
  const uint8_t *chunkData   = nullptr;
  size_t         chunkLength = 0;

  uint8_t channel() const;  // Channel Voice のときの下位ニブル
  uint8_t command() const;  // Channel Voice のときの上位ニブル、それ以外は status
};

} // namespace espmidi
```

**`status` / `data1` / `data2` を第一級フィールドに持つのは MIDI 1.0 の都合ですが、`raw` + `length` を併せ持つので器のサイズは固定されません。** MIDI 2.0 の 64 / 128 bit メッセージが来ても、`MessageType` を足して `raw` の長さが変わるだけです。

### ポインタの寿命

**`raw` と `chunkData` はコールバックの実行中だけ有効です。** 保存したい場合は呼び出し側がコピーします。

この規約は 2 箇所に適用されます。

1. ポートが core へメッセージを渡すとき(core はキューへコピーする)
2. core がユーザーのフィルタ / 変換コールバックへ渡すとき

長い SysEx をコピーなしで素通しできるのはこの規約のおかげです。`EspBle` の `EspBleMidiMessage` と同じ規約なので、BLE ポートは変換のときも規約が揃います。

## メッセージ種別は UMP の Message Type に揃える

`MessageType` の数値は UMP の Message Type と同じです。MIDI 2.0 対応時に番号を振り直さないためです。

| `MessageType` | 値 | UMP での意味 | 現状 |
| --- | --- | --- | --- |
| `Utility` | 0x0 | NOOP / JR Clock / JR Timestamp | 発生しない |
| `System` | 0x1 | System Common / System Real-Time | 発生する |
| `Midi1ChannelVoice` | 0x2 | MIDI 1.0 Channel Voice | 発生する |
| `Data7` | 0x3 | SysEx7 | 発生する |
| — | 0x4 | MIDI 2.0 Channel Voice | 将来 |
| — | 0x5 | SysEx8 / Mixed Data Set | 将来 |
| — | 0xD | Flex Data | 将来 |
| — | 0xF | UMP Stream | 将来 |

## チャンネル座標は (ポート, チャンネル)

MIDI 1.0 のチャンネルは 4 bit で 16 個しかありませんが、UMP は group(4 bit) × channel(4 bit) = 256 チャンネルです。`channel` だけを座標にすると 16 で行き止まりになります。

`PortId` が USB の cable / UMP の group を吸収するので、**座標は最初から `(PortId, channel)` の 256 空間**です。MIDI 1.0 と MIDI 2.0 でルーティング・フィルタの規則が変わりません。

## タイムスタンプは運ぶが解釈しない

BLE MIDI は 13 bit ミリ秒、MIDI 2.0 の JR Timestamp は 16 bit・1/31250 秒です。どちらも相対タイムスタンプで、単位が違うだけです。

`Timestamp` は**単位を型に持つ不透明値**として運びます。`EspMidi` は値を読んで並べ替えたりスケジュールしたりしません([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) の設計原則 3)。単位が違うポートへ転送するときは `TimestampUnit::None` に落とします。異なる方式間の厳密な時刻同期は要件外です。

## チャンクは SysEx 専用にしない

SysEx7(UMP の MT 0x3)、SysEx8(0x5)、Flex Data(0xD)はいずれも start / continue / end のチャンク構造を持ちます。`sysEx` 専用フラグではなく `chunk` / `chunkStart` / `chunkEnd` という汎用の形にしておくので、将来のデータメッセージがそのまま乗ります。

チャンクの分割位置そのものは意味を持ちません。意味を持つのは `chunkStart` と `chunkEnd` だけです。したがって `EspMidi` は**必要に応じてチャンクを分割します**(キューの 1 エントリに収まらない場合)。**結合はしません。** 分割しても `chunkStart` は最初の断片だけ、`chunkEnd` は最後の断片だけが持つので、下流から見れば 1 本のストリームのままです。チャンクの流れに関する規則は [ROUTING.ja.md](ROUTING.ja.md) にあります。

## 値の解像度

MIDI 1.0 は velocity / Control Change が 7 bit、MIDI 2.0 は velocity 16 bit、Controller 32 bit です。

**変換 API は「7 bit 固定」と名乗りません。** 「Velocity を 0..127 で指定する」という形の公開 API を作ると、MIDI 2.0 対応時に全部作り直しになります。MIDI 2.0 側に 1.0 ⇔ 2.0 のスケーリング(min-center-max)が規範として定義されているので、幅の広い版は後から非破壊で足せる形にしておきます。

フィルタの条件(ノート範囲、Control Change 番号)は幅に依存しないので、そのまま使えます。

## ポートモデル

階層は 2 段です。

```text
Endpoint(接続の単位。切断はここで起きる)
 └─ Port(ルーティングの座標。USB の cable、MIDI 2.0 の group に対応)
     ├─ In  Port
     └─ Out Port
```

```text
例:
  Endpoint "USB:addr3 Roland A-88"     ← 切断はこの単位で起きる
   ├─ In  Port 0  (cable 0)
   ├─ In  Port 1  (cable 1)
   └─ Out Port 0  (cable 0)

  Endpoint "BLE:xx:xx:xx WIDI Master"
   ├─ In  Port 0
   └─ Out Port 0

  Endpoint "UART1"
   ├─ In  Port 0
   └─ Out Port 0
```

### 1 エンドポイントあたり最大 16 ポート

USB MIDI 1.0 の cable は 4 bit(最大 16)、UMP の group も 4 bit(最大 16)です。この一致に合わせて上限を 16 とします。MIDI 2.0 に移ってもポート数の上限は変わりません。

### 入力ポートと出力ポートは別の実体

MIDI の物理も IN と OUT が独立している(DIN は別コネクタ、USB は別エンドポイント)ので、ルーティングの座標としても分けます。

ただし**どのエンドポイントに属するかは判別できます**。これがループ防止の既定を構造的に与えます([ROUTING.ja.md](ROUTING.ja.md))。

### ハンドルは論理的な席

`espmidi::InPort` / `espmidi::OutPort` は不透明ハンドルです。番号を受けて範囲チェックする公開 API は作らず、自動採番したハンドルを返します。

**ハンドルは席であり、機器が来たり去ったりします。**

```text
起動時          席 In#0 (UART1) と In#1 (USB Device cable0) が静的に存在
USB 機器接続    席 In#2 が現れる → onPortAdded
ルーティング     In#2 → Out#0 を設定
USB 機器切断    席 In#2 は残る。状態が「切断中」になる。ルーティング設定は保持
再接続          同じ席 In#2 が「利用可能」に戻る。ルーティングはそのまま動く
```

切断でハンドルが無効化されると、ルーティング設定が全部壊れてアプリケーションが再設定を強いられます。席モデルなら「接続中に構成を変更できる」「切断に追従できる」「無効になったポートを安全に扱える」が同時に満たせます。

切断中のポートへの送信は失敗を返しますが、致命的な扱いにはしません。

### 再接続時の席の照合

識別子で照合し、一致したら同じ席へ戻します。

| ポート | 識別子 |
| --- | --- |
| USB Host | VID / PID / serial |
| BLE Host | BLE アドレス |
| USB Device / BLE Device / UART | 静的な席なので照合不要 |

serial を持たない機器などで識別できない場合は、新しい席にします。

### 静的ポートと動的ポート

| ポート | 供給 |
| --- | --- |
| UART | 静的に 1 エンドポイント |
| USB Device | 静的に 1 エンドポイント(cable 数だけポートを持つ) |
| BLE Device | 静的に 1 エンドポイント |
| USB Host | **動的に 0〜N エンドポイント**(機器の接続で増減) |
| BLE Host | **動的に 0〜N エンドポイント** |
| アプリケーション | 静的に 1 エンドポイント(スケッチが作った数だけ) |

**アプリケーションポート**はトランスポートに裏打ちされないポートで、スケッチがメッセージを注入し受け取ります。`Transport::Application` として他のポートと同じ席・群・ループ規則に従います。詳細は [ROUTING.ja.md](ROUTING.ja.md) を参照してください。

ポートを供給するのはポート実装(アダプタ)です。**アダプタは「1 ポート」ではなく「ポートの供給者」**です。USB Host アダプタ 1 つが複数の機器を発見し、複数のエンドポイントと複数のポートを供給します。

### メタデータ

診断([REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) の可視性)とポート群の定義の両方に使うので、エンドポイントとポートは属性を持ちます。

- transport 種別(`usb_host` / `usb_device` / `ble` / `uart`)
- 名前(USB の product 文字列、BLE のデバイス名、cable の jack 名)
- 識別子(VID / PID / serial、BLE アドレス)
- 方向
- 状態(利用可能 / 切断中 / 未接続)

### ポート群

複数のポートを用途別の群としてまとめられます。ハンドルは自動採番です。

```cpp
espmidi::OutGroup synths = midi.addOutGroup("synths");
```

初期は**明示的な追加**と、「すべての入力」「すべての出力」の暗黙の群だけを持ちます。「条件に一致するポートは自動で参加」は、動的ポートの出入りと群の定義の相互作用が複雑になるので入れません。

暗黙の群は `InGroup::all()` / `OutGroup::all()` という**予約ハンドル**です。誰かが維持する群ではないので、**後から挿した機器も更新なしで含まれ**、「すべての出力へ」というルートが機器を挿すたびに壊れることがありません。群は方向で型が分かれているので、入力を出力群へ入れることはできません。

## MIDI 2.0 への地続き

現在の概念と将来の対応は次のとおりです。

| `EspMidi` | USB MIDI 1.0 | UMP / MIDI 2.0 |
| --- | --- | --- |
| Endpoint | USB デバイス + MIDI インターフェース | UMP Endpoint |
| Port | cable | **group** |
| ポート群 | — | **Function Block** に相当(名前と方向を持つ group の部分集合) |
| `MessageType` | CIN から導出 | **Message Type と同一の番号** |
| `Timestamp` | なし | JR Timestamp(`JrTicks31250`) |
| `chunk` | SysEx の分割 | SysEx7 / SysEx8 / Flex Data のチャンク |

MIDI 2.0 対応時にやることは「`MessageType` に 0x4 / 0x5 を足す」「64 / 128 bit 用のアクセサを足す」「MIDI 1.0 ⇔ 2.0 のスケーリングをポートの縁に置く」です。**ルーティング、フィルタ、ポート管理、ポート群のコードは触りません。**
