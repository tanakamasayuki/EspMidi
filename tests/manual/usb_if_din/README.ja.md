# manual/usb_if_din

**USB MIDI インターフェースと MIDI DIN 回路を 1 本の輪につないで、両方向を自動で確認します。**

手動なのは**治具を組むところまで**です。組み終われば `pytest` 一発で流れます。

```sh
uv run --env-file .env pytest manual/usb_if_din/usb_if_din.py
```

**ファイル名に `test_` が付いていないので、明示的に指定しない限り collect されません。** 治具が常時つながっていないテストなので、`pytest manual/` や `pytest` では動きません。

## 何を確認しているか

```text
        ┌──────── ESP32-S3(EspMidi の router 1つ)────────┐
        │  AppPort ──→ UartPort out ──→ TX ──[MIDI OUT回路]──┐
        │  AppPort ←── UsbHostPort in ←── USB ──┐            │
        │  AppPort ──→ UsbHostPort out ─→ USB ─┐│            │
        │  AppPort ←── UartPort in  ←── RX ←──┐││            │
        └─────────────────────────────────────┼┼┼────────────┘
                                              │││
                              ┌───────────────┴┴┴──┐
                              │  USB MIDI IF       │
                              │  DIN IN  ←─────────┼── ESP の MIDI OUT から
                              │  DIN OUT ──────────┼─→ ESP の MIDI IN へ
                              └────────────────────┘
```

**往路と復路が別の物理層を通ります。** DIN で出したものが USB で戻り、USB で出したものが DIN で戻ります。つまり **UART ポートと USB Host ポートが 1 つの router の中で噛み合っているか**を、実物のフォトカプラと他社ファームウェアごと通して確認できます。

| 確認するもの | 両方向 |
| --- | --- |
| ノート On / Off、CC、ピッチベンド | ○ |
| System Real-Time(Clock) | ○ |
| SysEx の往復 | ○ |
| **IF の席が名前を指定せずに出現すること** | — |

DIN 側は ch1(`0x90`)、USB 側は ch2(`0x91`)で送るので、**どちらの経路で戻ったのか取り違えられません**。

## 用意するもの

- **ESP32-S3** 1 台。USB ホストコネクタ(IF に 5V を供給できること)
- **USB MIDI インターフェース** 1 個。DIN の IN と OUT が両方あり、**MIDI Thru を持たないもの**
  - Thru を持つ IF は、DIN IN で受けたものを DIN OUT へ送り返します。**そうなると往路と復路が区別できず、この治具は成立しません**
  - テストは最初に 1 通試して `RIG_FAULT the interface merges DIN IN into DIN OUT` と言います
- **MIDI IN 回路と MIDI OUT 回路**。定数は [`../../../docs/HARDWARE.ja.md`](../../../docs/HARDWARE.ja.md)
  - MIDI OUT: **3.3V なら 33Ω(0.5W)と 10Ω**。5V の 220Ω × 2 ではありません
  - MIDI IN: フォトカプラ(PC-900V / 6N138)+ 220Ω + 1N914
- **MIDI ケーブル 2 本**

**GPIO に MIDI DIN を直結してはいけません。** 相手の電源基準の 5mA がそのまま 3.3V のピンに来ます。

## つなぎ方

**向きを間違えやすいところです。**

| ケーブル | から | へ |
| --- | --- | --- |
| 1 | ESP32 の MIDI OUT(TX ピン → 出力回路) | **IF の DIN IN** |
| 2 | **IF の DIN OUT** | ESP32 の MIDI IN(入力回路 → RX ピン) |

コンソールはボードの外付け USB-serial を使います。**USB ペリフェラルは Host 側に使うので、コンソールには使えません**(`sketch.yaml` は `USBMode=default`)。

## 設定

プロファイルは `arduino_smoke/` などと同じ **`esp32s3`** を使い回します。ボードの指定も同じ `TEST_SERIAL_PORT_ESP32S3` です。

```sh
# tests/.env
TEST_SERIAL_PORT_ESP32S3=/dev/ttyACM0

# MIDI DIN 回路につないだ GPIO
ESPMIDI_DIN_TX_PIN=17
ESPMIDI_DIN_RX_PIN=18
```

雛形は [`../../.env.example`](../../.env.example) にあります。

**一時的に変えたいときは、コマンドの前に置けます**(Linux / macOS のシェルの場合)。

```sh
ESPMIDI_DIN_TX_PIN=4 ESPMIDI_DIN_RX_PIN=5 \
  uv run --env-file .env pytest manual/usb_if_din/usb_if_din.py
```

**環境変数の指定方法は環境によって違います。** Windows の PowerShell なら `$env:ESPMIDI_DIN_TX_PIN = "4"`、cmd なら `set` です。恒久的に使うなら `.env` に書くのが確実です。

**ピンはビルドに焼き込みません。** 治具ごとに違うので、起動後にコンソールから渡します。

```text
host → board   "p 17 18"
board → host   "PINS tx=17 rx=18"
board → host   "USB_DEVICE in=1 out=1"
host → board   "g"
```

## 治具を組むときのコマンド

シリアルモニタ(115200)でボードにつなぐと、**全部 1 文字のコマンド**で確かめられます。**どれも実行したら戻ってくる**ので、つなぎ替えもリセットもせずに順に試せます。

```text
p 8 9      ← 最初に 1 回。ピンを渡してポートを開く
```

| | すること | 見どころ |
| --- | --- | --- |
| `1` | **DIN へ 1 通**出して 1 秒聞く | `RX_FROM_USB` が出れば往路が生きている。`RX_FROM_DIN` が出たら**自分に戻っている** |
| `2` | **USB へ 1 通**出して 1 秒聞く | `RX_FROM_DIN` が出れば復路が生きている |
| `r` | **2 秒聞くだけ**(何も送らない)。UART の生バイトを表示 | パーサを通さないので、**ドライバが受け取った実物**が見える |
| `t` | **5 秒間 DIN へ送り続ける** | ロジックアナライザ / オシロ用。アイドル High、3 バイトで約 0.96 ms、**31250 baud / 8-N-1 / LSB first** |
| `s` | 状態とカウンタ | 席の数、`noRoute` や `sendFailed` |
| `g` | **本番のテスト**(pytest が送るのと同じ) | |
| `?` | 一覧 | |

`NO_PINS` は `p` をまだ送っていないとき、`NO_USB` は IF が見つかっていないときです。

### ピンを変えたら電源を入れ直してください

**リセットボタンやシリアルモニタの開き直しでは足りません。** GPIO マトリクスの接続は**リセットの種類によっては残り**、コアの帳簿だけが初期化されます。

```text
s
UART1 rx=-1 tx=-1      ← コアは「未設定」と思っている
```

この状態でも、**前の実行で使ったピンがまだ U1TXD を出していることがあります**。`p` で新しいピンを指定しても、コアは古いピンを知らないので外しません。**指定していないピンから MIDI が出続けます。**

実際にこれで半日溶かしました。**症状は「配線していないはずの経路から信号が返ってくる」で、原因の切り分けを丸ごと狂わせます。**

- **ピンを変えるときは USB を抜いて電源ごと落とす**
- `s` の `UART1 rx= tx=` で、コアが持っている値は確認できます。**ただしマトリクスの実体とはずれ得ます**

同じ実行の中で `p` を打ち直すぶんには、`espmidi::UartPort::begin()` が古いシリアルを閉じるので解放されます([PORTS.ja.md](../../../docs/PORTS.ja.md))。

**`1` と `2` の 2 つで、どちらの経路がどこへ抜けているかがほぼ確定します。**

## 失敗したとき

**まず切り分けます。**

```sh
uv run --env-file .env pytest loopback/uart_midi/
```

こちらは**配線ゼロ**で通ります。**通るならソフトは正しい**ので、疑うのは回路・配線・IF です。

| 症状 | 疑うところ |
| --- | --- |
| `USB_NONE` | IF への 5V 供給、OTG ケーブル、IF が MIDI クラス準拠か |
| `RX_FROM_USB` が来ない | MIDI OUT 回路(抵抗値・ピン 4/5 の向き)、ケーブル 1 |
| `RX_FROM_DIN` が来ない | MIDI IN 回路(フォトカプラの向き、R_D、プルアップ)、ケーブル 2 |
| **`WRONG_PATH`** | **経路が輪になっていない。**下を参照 |
| **SysEx だけ来ない** | **IF の実装**。安い IF は SysEx を落とすことがあります |
| **`F8`(Clock)だけ来ない** | 同上。Real-Time を落とす IF があります |

**SysEx と Real-Time を落とすのは IF 側の欠陥で、このライブラリのバグではありません。** 疑わしいときは既知の良品でもう一度通してください。

### `RIG_FAULT the interface merges DIN IN into DIN OUT`

**IF が MIDI Thru を持っています。** 受けたものを送り返すので、往路と復路を区別できません。IF 側で切れないなら、**Thru を持たない IF に替えてください。**

**中継か電気的な回り込みかは、時間で分かります。** `1` を打つと各行に送信からの経過が出ます。

| `RX_FROM_DIN` の値 | 中身 |
| --- | --- |
| 約 960〜1000 µs | **電気的な回り込み**(送り終えた瞬間)。ESP 側の配線 |
| **約 1900 µs 以上** | **中継**(受け切ってから送り直している)。1 メッセージは 3 バイトで約 960 µs なので、その 2 倍 |

**印字の順番は根拠になりません。** `pump()` が `adapter.update()` を先に呼ぶので、同じ周回に溜まったものは必ず USB が先に並びます。**数字だけを見てください。**

### `WRONG_PATH want=message from USB got=message from DIN`

**DIN へ出したものが、USB ではなく自分の MIDI IN に戻ってきています。** 送信元・自分の MIDI IN・IF の DIN IN・IF の DIN OUT が 1 つの網につながった状態です。IF 側の受信が化けたり落ちたりするのも同じ原因です(2 つの送信元が同じループに電流を流すため)。

原因は 2 通りあります。**ケーブル 1(ESP の MIDI OUT → IF の DIN IN)だけ抜いて、もう一度流せば切り分けられます。**

| 抜いた状態 | 結論 |
| --- | --- |
| `RX_FROM_DIN` が**まだ出る** | **ESP 側の配線。** MIDI OUT が自分の MIDI IN に回り込んでいる |
| **出なくなる** | **IF が DIN IN を DIN OUT へ転送している**(Thru / マージ)。配線は正しいので、IF 側で切るか別の IF を使う |

**各ステップは経路を指定して待ちます。** どちらから来ても良い作りにすると、こういう輪になっていない治具でも先へ進んでしまい、**USB 経路が死んでいることに気付けません**。

## 記録

使った IF(型番)、回路の定数、GPIO を書き残します。IF ごとの癖はここに溜めていく価値があります。
