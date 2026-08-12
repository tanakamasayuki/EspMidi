# ポートの書き方

[English](PORT_AUTHORING.md)

**ポートは header-only なので、このリポジトリの外にも書けます。** RTP-MIDI、SPI ブリッジ、CV/Gate 変換、まだ存在しないトランスポート — どれも同梱ポートと同じ資格で参加できます。

この文書はその契約です。同梱ポートのうち [`src/EspMidiUart.h`](../src/EspMidiUart.h) が最も小さいので、読みながら並べると分かりやすいです。

## ポートがやること

**4 つだけです。**

1. **席を供給する** — `PortRegistry` にエンドポイントとポートを登録する
2. **受信を渡す** — `Router::receive()` を呼ぶ
3. **送信を引き受ける** — `setOutputSink()` で登録した関数でワイヤへ書く
4. **状態を反映する** — 繋がったか切れたかをレジストリに伝える

**それ以外はやりません。** ルーティング、フィルタ、キュー、ループ規則、SysEx の 3 規則はすべて core の仕事です。

## 骨格

```cpp
#include "EspMidi.h"

namespace espmidi
{

template <typename TransportType>
class BasicMyPort
{
public:
  BasicMyPort(Router &router, TransportType &transport, uint8_t index = 0)
      : router_(router), transport_(transport), index_(index) {}

  bool begin(const char *name = "My MIDI")
  {
    EndpointIdentity identity;
    identity.transport = Transport::Uart;   // 近いものを選ぶ
    identity.index = index_;

    endpoint_ = router_.registry().attachEndpoint(identity, name);
    if (!endpoint_.valid()) return false;

    in_ = router_.registry().attachInPort(endpoint_, 0);
    out_ = router_.registry().attachOutPort(endpoint_, 0);
    if (!in_.valid() || !out_.valid()) return false;

    return router_.setOutputSink(out_, &BasicMyPort::sendFrom, this);
  }

  void update()
  {
    // ワイヤから読み、Message にして router_.receive() へ渡す
  }

  InPort in() const { return in_; }
  OutPort out() const { return out_; }

private:
  static bool sendFrom(void *context, const Message &message)
  {
    return static_cast<BasicMyPort *>(context)->send(message);
  }

  bool send(const Message &message)
  {
    // ワイヤへ書く。拒否されたら false
  }

  Router &router_;
  TransportType &transport_;
  uint8_t index_ = 0;
  EndpointId endpoint_;
  InPort in_;
  OutPort out_;
};

#if defined(ARDUINO)
using MyPort = BasicMyPort<RealTransport>;
#endif

} // namespace espmidi
```

## 守るべき規約

### 1. スタックを所有しない

**`begin()` も `end()` もしません。** スケッチが起動したトランスポートを**参照で受け取る**だけです。これが MIDI と HID / CDC / 独自 GATT の共存を可能にしています([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md))。

### 2. `begin()` は冪等にする

再設定は「もう一度 `begin()`」で済むようにします。`attachEndpoint()` と `attachInPort()` が冪等なので、**同じ引数なら同じ席が返ります**。新しい席を作ってはいけません — スケッチのルートが古い席を指したままになります。

### 3. 席は消さない

切断では `detachEndpoint()` を呼び、**状態だけを `Disconnected` にします**。`PortRegistry` に席を削除する API はありません。これが「抜き差ししてもルートを張り直さなくてよい」の根拠です([DATA_MODEL.ja.md](DATA_MODEL.ja.md))。

### 4. 動的なポートは識別子で照合する

機器が来たり去ったりするトランスポートでは、`EndpointIdentity` に**再接続で一致する情報**を入れます。

| 使えるもの | 使ってはいけないもの |
| --- | --- |
| VID / PID / シリアル、MAC アドレス、固定の URL | **そのとき割り当てられた番号**(USB アドレス、接続 ID) |

識別できない機器は**毎回新しい席**になります。それが正しい挙動です(前の機器のルーティングを別の機器に渡さないため)。

### 5. 受信は「どのタスクからでも」よい

`Router::receive()` は**スレッドセーフな唯一の入口**です。トランスポートのコールバックからそのまま呼べます。

**ただしそこで他のことをしてはいけません。** 席を作る、レジストリを読む、送信する — どれも `update()`(スケッチのタスク)側でやります。共有する状態はできるだけ小さくします。

同梱ポートの実例:

| ポート | コールバックでやること |
| --- | --- |
| UART / USB Device | ポーリングなので、そもそも別タスクが無い |
| USB Host | **生パケット 4 バイトをロックフリーのリングに写すだけ**。デコードは `update()` |
| BLE | **`receive()` を直接呼ぶ**。共有するのは接続ごとのポートハンドル 1 語だけ |

BLE が直接呼べるのは、キューがコピーするおかげで**ダンプがコピーされない**からです。USB Host がリングを挟むのは、`EspUsbHost` が 1 パケットずつ独自のバッファで渡してくるからです。**トランスポートの形で答えが変わります。**

### 6. ポインタは渡してよい

`Message::raw` と `chunkData` は**ワイヤのバッファをそのまま指して**渡せます。`receive()` がキューへ写すので、戻った後の寿命を気にしなくてよいです。**長い SysEx がコピーなしで通るのはこのおかげです。**

### 7. 送れないときは false を返す

未接続、FIFO 満杯、範囲外 — どれも sink から `false` を返します。core が `sendFailed` に数えます。**黙って捨ててはいけません。**

### 8. 読み込みに上限を設ける

`update()` が 1 回で読む量を必ず区切ります。ダンプを流す機器が `loop()` を占有すると、他のポートが止まります。

```cpp
#ifndef ESPMIDI_MYPORT_RX_BYTES
#define ESPMIDI_MYPORT_RX_BYTES 64
#endif
```

読み残しはトランスポートのバッファに残るので、次の `update()` で続きます。

### 9. 診断を出す

core のカウンタでは説明できない破棄が起きるなら、**自分で数えて公開します**。

```cpp
uint32_t unknownCablePackets() const;   // 宣言していない cable に来た
uint32_t droppedPackets() const;        // リングがあふれた
uint32_t refusedDevices() const;        // 席が尽きた
```

「動かない」を切り分けられるかどうかがここで決まります。

### 10. 中断したストリームを閉じる

送信中に相手がいなくなったら、**可能なら `0xF7` を送ってから**閉じます(規則 2)。送れないなら畳むだけにして、**次のダンプが前の続きにならない**ようにします。

## テストできる形にする

**トランスポートをテンプレートパラメータにしてください。** 同梱ポートは全部そうなっています。

```cpp
template <typename TransportType> class BasicMyPort { ... };
#if defined(ARDUINO)
using MyPort = BasicMyPort<RealTransport>;
#endif
```

こうすると偽物を当ててホスト上で回せます。**席の供給、受信が router へ届くこと、枠付け、満杯のときの挙動、切断の扱い** — 実機なしで全部固定できます。

```cpp
struct FakeTransport {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  // ポートが呼ぶメソッドだけ持つ
};
using TestPort = espmidi::BasicMyPort<FakeTransport>;
```

**実機のテストに残るのは、実機でしか確認できないことだけ**になります。同梱ポートでは「バイトが本当にパッドを渡ること」だけが残りました([../tests/unit/README.ja.md](../tests/unit/README.ja.md))。

### 型を名指ししない書き方

トランスポート側の構造体名を書かずに済ませられると、テストの偽物が楽になります。同梱ポートでは 2 つの方法を使っています。

**シグネチャから推論する:**

```cpp
template <typename T, typename P>
P deviceInfoType(size_t (T::*)(P *, size_t) const);

using DeviceInfo = decltype(deviceInfoType(&HostType::getDevices));
```

**総称ラムダで受ける:**

```cpp
transport_.onMessage([this](const auto &message) { onMessage(message); });
```

## やってはいけないこと

| | なぜ |
| --- | --- |
| sink から他のポートへ送る | ルーティングの仕事。ループ規則も効かなくなる |
| `receive()` の中で送信する | トランスポートのタスクで別のトランスポートを触ることになる |
| 自前のタスクを立てる | core は立てない。駆動は `loop()`([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)) |
| SysEx の中身を解釈する | 機器固有。運ぶだけにする |
| タイムスタンプを読んで並べ替える | 運ぶが解釈しない |
| 席を削除する | ルートが壊れる |
| ヒープを使う | 記憶域は固定長。`ESPMIDI_*` で調整できる形にする |

## チェックリスト

- [ ] スタックを参照で受け取り、`begin()` / `end()` しない
- [ ] `begin()` が冪等で、同じ席を返す
- [ ] 切断で席を消さず、状態だけ変える
- [ ] 動的なら識別子で照合し、識別できなければ新しい席にする
- [ ] `receive()` 以外を他タスクから呼んでいない
- [ ] 送れないとき `false` を返す
- [ ] `update()` の読み込みに上限がある
- [ ] 独自の破棄を数えて公開している
- [ ] 中断したストリームを閉じる
- [ ] テンプレートにしてホスト上のテストがある
- [ ] 固定長記憶域で、上限が `#define` で変えられる

## 参考にする順

1. [`src/EspMidiUart.h`](../src/EspMidiUart.h) — 最小。静的な席 1 組
2. [`src/EspMidiEspUsbDevice.h`](../src/EspMidiEspUsbDevice.h) — cable ごとの席、mount 状態
3. [`src/EspMidiEspBle.h`](../src/EspMidiEspBle.h) — 別タスクからの `receive()`、ダンプの再組み立て
4. [`src/EspMidiEspUsbHost.h`](../src/EspMidiEspUsbHost.h) — 動的な席、リング、polling による発見

テストは `tests/unit/uart_port` から順に同じ並びです。
