# 使い方ガイド

順に読めば、1 ポートの送信から複数ポートのルーティングまで進めるようにしてあります。**MIDI 自体に不慣れな場合は [MIDI_BASICS.ja.md](MIDI_BASICS.ja.md) を先に**読んでください。ハマりどころはほとんどが MIDI 側の仕様です。

各節は `examples/` の実物に対応しています。読むだけでなく書き込んで動かせます。

## 0. どのスケッチも同じ 3 段

```cpp
void setup() {
  // 1) スタックを起動する ── スケッチの仕事。EspMidi は USB も BLE も所有しない
  usb.begin(config);

  // 2) ポートを作る ── ここから先が EspMidi
  port.begin("USB MIDI");

  // 3) ルートを張る
  router.addRoute(port.in(), somewhere.out());
}

void loop() {
  usb.task();      // スタック
  port.update();   // ポート(受信を取り込む)
  router.update(); // ルーティングを走らせる
}
```

**`router.update()` を呼ばないと何も動きません。** 送信も受信もここで走ります。これが最初のつまずきどころです。

なぜこの形かというと、受信は USB や BLE の**別タスク**から届くからです。そこで直接送り返すとトランスポートの都合がアプリに漏れます。受け取ったものはキューに写して、パイプラインは `loop()` の中だけで走らせます([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md))。

## 1. 送ってみる

→ [`examples/SimpleMidiOut`](../examples/SimpleMidiOut/)

```cpp
espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::UartPort din(router, Serial1, 1);
espmidi::AppPort sketch(router, "sketch");

void setup() {
  din.begin("MIDI OUT", -1, TX_PIN);   // rxPin = -1: 送信だけ
  router.addRoute(sketch.in(), din.out());
}

void loop() {
  sketch.sendShort(0x90, 60, 100);     // ノートオン(まだ送られない)
  router.update();                     // ここで線に出る
  delay(250);
  sketch.sendShort(0x80, 60, 0);       // ノートオフ
  router.update();
}
```

**`espmidi::AppPort` は「スケッチというポート」です。** トランスポートの裏付けがないポートで、ほかのポートと同じようにルーティングされます。だから後から「PC にも同時に送る」と言われても、**ルートを 1 本足すだけ**で済みます。

**ノートオフを忘れないでください。** 鳴り続けます。

## 2. 受けてみる

→ [`examples/SimpleMidiIn`](../examples/SimpleMidiIn/)

```cpp
void onMidi(void *, const espmidi::Message &message) {
  if (message.chunk) return;            // SysEx は後回し
  Serial.println(message.status, HEX);
}

void setup() {
  din.begin("MIDI IN", RX_PIN, -1);
  router.addRoute(din.in(), sketch.out());
  sketch.onMessage(onMidi);
}
```

コールバックは `router.update()` の中、**スケッチのタスクで**呼ばれます。だから中で `Serial.print()` しても、ほかのトランスポートを止めません(ただし長く居座れば全体が遅れます)。

`message` の中のポインタ(`raw` / `chunkData`)は**そのコールバックの間だけ有効**です。後で使うならコピーします。

## 3. 同じコードで別のインターフェース

→ [`examples/SameCodeAnyPort`](../examples/SameCodeAnyPort/)

**ポートが 1 つだけでもこのライブラリを使う理由がここです。**

```cpp
#if MIDI_PORT == MIDI_PORT_UART
  espmidi::UartPort port(router, Serial1, 1);
#elif MIDI_PORT == MIDI_PORT_USB
  espmidi::UsbDevicePort port(router, usbMidi, usb);
#else
  espmidi::BleDevicePort port(router, bleMidi);
#endif
```

**この `#if` から下は 1 文字も変わりません。** ルート、フィルタ、変換、受信の扱いはすべて共通です。

- UART で作って、あとで USB MIDI にする → ポートの宣言と `begin()` だけ
- USB で作って、BLE も足す → ポートを 1 つ増やしてルートを 1 本足す
- 「SysEx はどう届くの?」「velocity 0 は?」→ **どのトランスポートでも同じ答え**

トランスポートごとの API を直接使うと、この差分が**アプリ側のコードに散ります**。EspMidi の共通表現に一度寄せておくと、そこが動きません。

## 4. フィルタと変換はコードではなく宣言で

```cpp
const espmidi::Route route = router.addRoute(keys.in(), bass.out());

espmidi::Filter lower;                     // 鍵盤の下半分だけ
lower.kinds = espmidi::KindNotes;
lower.noteMin = 36;
lower.noteMax = 59;
router.setRouteFilter(route, lower);

espmidi::Transform toBass;                 // 1 オクターブ下げて ch2 へ
toBass.transpose = -12;
toBass.channel = 1;                        // 0 始まり = 機器の ch2
router.setRouteTransform(route, toBass);
```

段は**フィルタ → 変換 → コールバック**の順に適用され、**ルート・入力ポート・出力ポートのどこにでも**同じ形で置けます。

- 「この機器は 1 オクターブ下」→ **入力ポート**に置く
- 「この経路は半音上げる」→ **ルート**に置く
- 「この音源は ch6 で聴く」→ **出力ポート**に置く

3 つは独立に書けて、順に適用されます。詳細は [ROUTING.ja.md](ROUTING.ja.md)。

**velocity 0 のノートオンは変換しません。** ワイヤ上はノートオフなので、スケールすると「止まらない小さな音」になります。

## 5. 複数ポート

```cpp
// 1 対多: 1 つの演奏を音源と PC へ
router.addRoute(keys.in(), din.out());
router.addRoute(keys.in(), pc.out(0));

// 多対 1: 全部の入力を 1 つの音源へ
router.addRoute(espmidi::InGroup::all(), din.out());
```

**ルートは来た元のエンドポイントへは返しません。** だから「全入力 → 全出力」と書いても、自分の入力が自分の出力へ回りません。意図的に返したいときだけ `setRouteAllowSameEndpoint(route, true)` を使います(MIDI Thru を作るときなど)。

**暗黙の全結合はありません。** ルートを 1 本も作らなければ何も転送されません。

## 6. 抜き差しされるポート

USB Host と BLE Host のポートは**挿したときに現れます**。スケッチに機器名を書けません。

```cpp
// 機器を名指ししない
router.addRoute(espmidi::InGroup::all(), din.out());
```

これで、あとから挿した機器も勝手に参加します。**抜いてもルートは消えません** — ポートのハンドルは切断で無効にならず、状態だけが `Disconnected` に変わります(「席」モデル。[DATA_MODEL.ja.md](DATA_MODEL.ja.md))。挿し直せばそのまま続きます。

席の出入りを知りたいときは通知を使います。

```cpp
registry.addListener([](void *, const espmidi::PortEvent &event) {
  espmidi::PortInfo info;
  if (registry.portInfo(event.port, info)) Serial.println(info.name);
});
```

イベントは「席が現れた」と「席の状態が変わった」の 2 つだけです。**席は削除されません。**

## 7. つまみ・ボタン・クロック

```cpp
espmidi::Analog knob(sketch);
espmidi::Button button(sketch);

void loop() {
  knob.update(analogRead(KNOB_PIN));
  button.update(digitalRead(BUTTON_PIN) == LOW, millis());
  router.update();
}
```

**ヘルパーはピンにも時刻にも触りません。** 読んだ値と今の時刻を渡します。だから ADC でもポートエキスパンダでもタッチセンサでも同じものが使えます。→ [`examples/GpioControls`](../examples/GpioControls/)

## よくあるつまずき

### 音が出ない / 何も届かない

順に確認します。

1. **`router.update()` を呼んでいますか。** ポートの `update()` も必要です(受信を取り込むのはポート側)。
2. **ルートを張りましたか。** 暗黙の全結合はありません。
3. **ポートは使える状態ですか。** USB Device は PC が構成するまで、BLE Device は相手が購読するまで送れません。

```cpp
Serial.println(registry.portAvailable(port.out(0).port) ? "ready" : "not ready");
```

4. **診断カウンタを見ます。**

```cpp
const espmidi::RouterCounters c = router.counters();
Serial.printf("recv=%u deliv=%u noRoute=%u sendFailed=%u full=%u filtered=%u\n",
              c.received, c.delivered, c.noRoute, c.sendFailed, c.queueFull, c.droppedByFilter);
```

| 増えているもの | 意味 |
| --- | --- |
| `received` が 0 | そもそも届いていない。配線・絶縁・ボーレート・ポートの `update()` |
| `noRoute` | ルートが無い、または無効 |
| `droppedByFilter` | フィルタで落ちている |
| `sendFailed` | トランスポートが拒否した(未接続、FIFO 満杯) |
| `queueFull` | `update()` の間隔が長すぎる、またはキューが浅い |
| `blockedBySysEx` | SysEx 送信中に通常メッセージが来た(規則 3) |

### 音が鳴り止まない

**velocity 0 のノートオンはノートオフです。** 自分で判定するときは両方を見てください(`espmidi::messageKind()` はこれを正しく分類します)。ノートオフを送り忘れているケースも定番です。

### 二重に鳴る / 止まらなくなる

- ルートを二重に張っていませんか(同じ組み合わせを 2 回 `addRoute`)
- `setRouteAllowSameEndpoint(true)` を使った経路がループになっていませんか
- **2 つのポートが同じ物理リンクの両端**の場合、ループ規則では検出できません(別エンドポイントなので)。その 2 つを直結しないでください

### つまみが CC を出し続ける

ADC は勝手に揺れます。`hysteresis`(既定 8 カウント)を上げてください。

### SysEx が途切れる / 送れない

- BLE は既定 320 バイトまでです(`ESPMIDI_BLE_SYSEX_BYTES`)。超えると拒否して数えます(`oversizedStreams()`)
- SysEx 送信中の通常メッセージは**捨てられます**(規則 3)。仕様です
- ダンプの経路は**開始時に確定**します(規則 1)。途中でルートを変えても切り替わりません

### 抜き差しでルートを張り直したくなる

要りません。席は残ります。張り直すと**ルートが二重になります**。

## 次に読むもの

| 知りたいこと | 文書 |
| --- | --- |
| MIDI 自体の注意点、インターフェース別の注意点 | [MIDI_BASICS.ja.md](MIDI_BASICS.ja.md) |
| 使うポートの詳細と制約 | [PORTS.ja.md](PORTS.ja.md) |
| ルーティングの規則(SysEx、ループ) | [ROUTING.ja.md](ROUTING.ja.md) |
| メッセージとポートの形 | [DATA_MODEL.ja.md](DATA_MODEL.ja.md) |
| 固定長記憶域の調整 | [CONFIGURATION.ja.md](CONFIGURATION.ja.md) と各ポートの `ESPMIDI_*` |
| 自作ポートを書く | `src/EspMidi.h` のコメント → `src/EspMidiUart.h` |
