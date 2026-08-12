# レシピ集

[English](RECIPES.md)

「これをやりたい」から引く短い断片集です。**順に読む入門は [GUIDE.ja.md](GUIDE.ja.md)**、名前を引くのは [API.ja.md](API.ja.md) です。

前提として、どの断片も次があるものとします。

```cpp
espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::AppPort sketch(router, "sketch");
```

**この文書のコードは `tests/unit/docs_snippets` でコンパイルされています。** 名前が変わったら落ちるので、ここに古い API は残りません。

## 経路を作る

### MIDI Thru を作る

入ってきたものを同じ機器へ返します。**既定では返らない**ので明示的に解除します。

```cpp
const espmidi::Route thru = router.addRoute(din.in(), din.out());
router.setRouteAllowSameEndpoint(thru, true);
```

**相手も Thru を持っていると無限ループになります。** 片方だけにしてください。

### 2 つの入力を 1 つの音源へまとめる

```cpp
router.addRoute(keysA.in(), synth.out());
router.addRoute(keysB.in(), synth.out());
```

あるいは「全部」でまとめます。あとから挿した機器も自動で入ります。

```cpp
router.addRoute(espmidi::InGroup::all(), synth.out());
```

### 1 つの演奏を複数へ配る

```cpp
router.addRoute(keys.in(), synth.out());
router.addRoute(keys.in(), pc.out(0));
router.addRoute(keys.in(), recorder.out());
```

### 用途別のグループを作る

```cpp
const espmidi::OutGroup synths = registry.addOutGroup("synths");
registry.addToGroup(synths, moduleA.out());
registry.addToGroup(synths, moduleB.out());
router.addRoute(keys.in(), synths);
```

### 一時的に経路を切る

削除ではなく無効化なら、ハンドルもフィルタ設定も残ります。

```cpp
router.setRouteEnabled(route, false);
// ... あとで
router.setRouteEnabled(route, true);
```

## 絞る・書き換える

### 鍵盤を左右に分ける

```cpp
const espmidi::Route bass = router.addRoute(keys.in(), bassModule.out());
espmidi::Filter lower;
lower.kinds = espmidi::KindNotes;
lower.noteMin = 36;
lower.noteMax = 59;
router.setRouteFilter(bass, lower);

const espmidi::Route lead = router.addRoute(keys.in(), leadModule.out());
espmidi::Filter upper;
upper.kinds = espmidi::KindNotes;
upper.noteMin = 60;
upper.noteMax = 96;
router.setRouteFilter(lead, upper);
```

**ノートオフも同じ範囲を通ります。** そうでないと音が止まりません。

### 1 オクターブ下げる

```cpp
espmidi::Transform down;
down.transpose = -12;
router.setRouteTransform(route, down);
```

範囲外に出たノートは**折り返さずに捨てます**(鍵盤の反対端で鳴らないため)。

### 特定のチャンネルだけ通す

```cpp
espmidi::Filter drumsOnly;
drumsOnly.allowOnlyChannel(9);   // 0 始まり = 機器の ch10
router.setRouteFilter(route, drumsOnly);
```

### チャンネルを付け替える

```cpp
espmidi::Transform toChannel3;
toChannel3.channel = 2;          // 0 始まり = 機器の ch3
router.setRouteTransform(route, toChannel3);
```

### CC 番号を付け替える

**フィルタで絞って、変換で番号を設定します。** 付け替え表は持ちません。

```cpp
espmidi::Filter onlyVolume;
onlyVolume.kinds = espmidi::KindControlChange;
onlyVolume.ccMin = onlyVolume.ccMax = 7;
router.setRouteFilter(route, onlyVolume);

espmidi::Transform toExpression;
toExpression.controller = 11;
router.setRouteTransform(route, toExpression);
```

複数の付け替えは**複数のルート**になります。

### 音量を抑える

```cpp
espmidi::Transform quieter;
quieter.velocity = espmidi::ValueMap::scale7(0, 100);   // 最大 100 まで
router.setRouteTransform(route, quieter);
```

### タッチ非対応の音源へ一定の強さで送る

```cpp
espmidi::Transform flat;
flat.velocity = espmidi::ValueMap::fixed7(100);
router.setRouteTransform(route, flat);
```

### ペダルの向きを直す

```cpp
espmidi::Transform reversed;
reversed.controllerValue = espmidi::ValueMap::range7(0, 127, 127, 0);
router.setRouteTransform(route, reversed);
```

### Clock を落とす

1 拍 24 通あるので、モニタやログでは邪魔になります。

```cpp
espmidi::Filter quiet;
quiet.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindSystemRealTime);
router.setOutPortFilter(monitor.out(), quiet);
```

### 音色ダンプを通さない

```cpp
espmidi::Filter noData;
noData.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindData);
router.setRouteFilter(route, noData);
```

### 「この機器は常に 1 オクターブ下」

ルートではなく**入力ポート**に置きます。その機器から出るものすべてに効きます。

```cpp
espmidi::Transform deviceIsLow;
deviceIsLow.noteOffset = 12;
router.setInPortTransform(oldKeyboard.in(), deviceIsLow);
```

`noteOffset` と `transpose` が別なのは、**機器の癖とルートの意図を独立に書ける**ようにするためです。

### コードで判断する

宣言で書けないものはコールバックにします。

```cpp
espmidi::Verdict onlyLoudNotes(void *, espmidi::Message &message) {
  if ((espmidi::messageKind(message) & espmidi::KindNoteOn) != 0 && message.data2 < 40) {
    return espmidi::Verdict::Drop;
  }
  return espmidi::Verdict::Pass;
}
router.setRouteCallback(route, onlyLoudNotes);
```

**1 → N や別種のメッセージはここでは作れません。** アプリケーションポートへ `send()` してください。

## 見る・作る

### 経路を邪魔せずに監視する

**通す経路とは別のルート**を引きます。表示が遅くても音は遅れません。

```cpp
router.addRoute(keys.in(), synth.out());     // 音の経路
router.addRoute(keys.in(), monitor.out());   // 監視の経路
monitor.onMessage(printMessage);
```

### つまみを CC にする

```cpp
espmidi::Analog knob(sketch);
knob.config().controller = 7;

void loop() {
  knob.update(analogRead(KNOB_PIN));
  router.update();
}
```

### ボタンをサステインペダルにする

```cpp
espmidi::Button pedal(sketch);
pedal.config().note = false;
pedal.config().number = 64;   // Sustain

void loop() {
  pedal.update(digitalRead(PEDAL_PIN) == LOW, millis());
  router.update();
}
```

`config().latch = true` にすると、モーメンタリスイッチがトグルになります。

### LED をノートに追従させる

```cpp
espmidi::Filter note60;
note60.kinds = espmidi::KindNotes;
note60.noteMin = note60.noteMax = 60;
espmidi::ControlOutput lamp(note60, setLed);

// AppPort に直接つなげる形
watcher.onMessage(&espmidi::ControlOutput::receive, &lamp);
```

**velocity 0 のノートオンは 0 になる**ので、LED が点いたままになりません。

### MIDI Clock を出す

```cpp
espmidi::ClockGenerator clock(sketch);
clock.setTempo(12000);          // 120.00 BPM
clock.start(micros());

void loop() {
  clock.update(micros());
  router.update();
}
```

### 外部クロックを測って作り直す

```cpp
espmidi::ClockCounter counter;
espmidi::ClockGenerator clock(sketch);

void onMidi(void *, const espmidi::Message &message) {
  counter.handle(message, micros());
}

void loop() {
  if (counter.microsPerTick() != 0) {
    clock.setMicrosPerTick(counter.microsPerTick());
  }
  clock.update(micros());
  router.update();
}
```

`counter.onQuarter()` が拍の頭で真になるので、LED を点滅させられます。

### 音色ダンプを送る

```cpp
const uint8_t payload[] = {0x7d, 0x01, 0x02};   // 0xF0 と 0xF7 は付けない
espmidi::Message dump;
dump.type = espmidi::MessageType::Data7;
dump.status = 0xf0;
dump.chunk = true;
dump.chunkStart = true;
dump.chunkEnd = true;
dump.chunkData = payload;
dump.chunkLength = sizeof(payload);
sketch.send(dump);
router.update();
```

**枠付け(`0xF0` / `0xF7`)はポートが付けます。** 長いダンプは `chunkStart` だけの断片、両方立てない断片、`chunkEnd` だけの断片に分けて送れます。

## 機器の出入りに対応する

### 挿された機器を自動で参加させる

**機器を名指ししないのがコツです。**

```cpp
router.addRoute(espmidi::InGroup::all(), synth.out());
```

抜いてもルートは残り、挿し直せば続きます。**張り直すと二重になります。**

### 出入りを知る

```cpp
void onPortEvent(void *, const espmidi::PortEvent &event) {
  espmidi::PortInfo info;
  if (!registry.portInfo(event.port, info)) return;
  Serial.print(info.name);
  Serial.println(info.state == espmidi::PortState::Available ? " available" : " disconnected");
}
registry.addListener(onPortEvent);
```

イベントは「現れた」と「状態が変わった」の 2 つだけです。

### 特定の機器だけ扱う

```cpp
espmidi::PortInfo info;
if (registry.portInfo(port, info) && strcmp(info.name, "A-88") == 0) {
  router.addRoute(espmidi::InPort{port}, synth.out());
}
```

### 送れるかどうか先に見る

```cpp
if (registry.portAvailable(pc.out(0).port)) {
  sketch.sendShort(0x90, 60, 100);
}
```

見なくても失敗は `sendFailed` に出るので、**どちらでも壊れません**。

## 困ったとき

### 定期的に診断を出す

```cpp
void loop() {
  router.update();

  static uint32_t next = 0;
  if (millis() >= next) {
    next = millis() + 5000;
    const espmidi::RouterCounters c = router.counters();
    Serial.printf("recv=%u deliv=%u noRoute=%u failed=%u full=%u\n",
                  c.received, c.delivered, c.noRoute, c.sendFailed, c.queueFull);
  }
}
```

読み方の表は [GUIDE.ja.md](GUIDE.ja.md) にあります。

### RAM を削る

```cpp
#define ESPMIDI_MAX_PORTS 8
#define ESPMIDI_QUEUE_ENTRIES 8
#define ESPMIDI_MAX_ROUTES 4
#include <EspMidiUart.h>
```

実測の削減量は [FOOTPRINT.ja.md](FOOTPRINT.ja.md) にあります。**UART 2 本なら core は約 1.8KB です。**

### 全部の音を止める

MIDI を解釈しないので、自動では送りません。必要ならスケッチから送ります。

```cpp
for (uint8_t channel = 0; channel < 16; channel++) {
  sketch.sendShort(static_cast<uint8_t>(0xb0 | channel), 123, 0);   // All Notes Off
}
router.update();
```
