# 開発計画

実装順、現在地、残作業です。設計の正本は [DATA_MODEL.ja.md](DATA_MODEL.ja.md) / [ROUTING.ja.md](ROUTING.ja.md) / [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)、判断の理由は [DECISIONS.ja.md](DECISIONS.ja.md) です。

## 現在地

**Phase 9 完了。実装計画は完了しました。** core、同梱ポートの全部、Control Mapping ヘルパーが揃っています。**ポートはすべて実機で検証済みです。**

- `src/EspMidiMessage.h` — `Message` / `PortId` / `Timestamp` / `MessageType`、ステータス分類とデータ長表、短いメッセージの構築と直列化
- `src/EspMidiParser.h` — MIDI 1.0 バイトストリーム ⇄ `Message`。`Parser` が受信、`Serializer` が送信
- `src/EspMidiUsbPacket.h` — USB MIDI イベントパケット ⇄ `Message`(USB Host / Device 共用)
- `src/EspMidiPort.h` — `PortRegistry`。Endpoint / InPort / OutPort、席モデル、状態、メタデータ、ポート群、通知
- `src/EspMidiRouter.h` — `Router` と `AppPort`。ルート、3 段パイプライン、キュー駆動、SysEx 3 規則、ループ規則、診断カウンタ
- `src/EspMidiFilter.h` — `Filter` / `Transform` / `ValueMap`。宣言的な絞り込みと書き換え
- `src/EspMidiUart.h` — `UartPort`。31250 baud の `HardwareSerial` に乗る最初のポート
- `src/EspMidiEspUsbDevice.h` — `UsbDevicePort`。`EspUsbDevice` の cable 数だけポートを供給する
- `src/EspMidiEspUsbHost.h` — `UsbHostPort`。接続された機器ごとに席を動的に供給する
- `src/EspMidiEspBle.h` — `BleDevicePort` / `BleHostPort`。タイムスタンプを持つ唯一のポート
- `src/EspMidiControl.h` — `Button` / `Analog` / `Encoder` / `ControlOutput` / `ClockGenerator` / `ClockCounter`

**実装計画は完了しました。** core、ポート、Control Mapping ヘルパーが揃い、ハードウェアに依存しない部分はすべてホスト上のテストで固定されています。

実機で確認したのは次のとおりです。`loopback/uart_midi`(1 台・**配線ゼロ**)、`peer/uart_midi`(2 台・既存配線をクロス)、`peer/usb_midi`(素の `EspUsbHost` が観測役。**cable 数を descriptor から読んで assert**)、`peer/usb_midi_host`(両端が `EspMidi`。**動的な席の出現**)、`peer/ble_midi`(無線・**タイムスタンプ**とダンプの往復)。

兄弟ライブラリの依存は**公開バージョン**です(`EspUsbHost` 2.7.5 / `EspUsbDevice` 2.0.2)。依頼 1・2 の cable 対応はどちらもリリース済みなので、`*_local` プロファイルは開発版を試すときだけ使います。

## 実装順

Phase 1〜4 はすべてホスト上で完結するので、実機なしで core が完成します。

| Phase | 内容 | テスト | 状況 |
| --- | --- | --- | --- |
| 0 | リポジトリ骨格、docs、テスト環境、CI | `unit/test_repository_structure.py`、`arduino_smoke/` | **完了** |
| 1 | 共通表現。`Message` / `PortId` / `Timestamp` / `MessageType` と MIDI 1.0 バイトストリームのパーサ(running status、SysEx チャンク)、USB パケットコーデック | `unit/message`、`unit/parser`、`unit/usb_packet` | **完了** |
| 2 | ポートモデル。Endpoint / Port / 席 / 状態 / メタデータ / ポート群 | `unit/port_model` | **完了** |
| 3 | ルーティングと駆動。Route / 3 段パイプライン / キュー / SysEx 3 規則 / アプリケーションポート | `unit/routing`、`unit/sysex_rules` | **完了** |
| 4 | フィルタと変換 | `unit/filter`、`unit/transform` | **完了** |
| 5 | UART ポート | `unit/serializer`、`unit/uart_port`、`loopback/uart_midi`(配線ゼロ)、`peer/uart_midi` | **完了(実機検証済み)** |
| 6 | USB Device ポート | `unit/usb_device_port`、`peer/usb_midi` | **完了(実機検証済み)** |
| 7 | USB Host ポート | `unit/usb_host_port`、`unit/concurrent_receive`、`peer/usb_midi_host` | **完了(実機検証済み)** |
| 8 | BLE ポート(Device / Host) | `unit/ble_port`、`peer/ble_midi` | **完了(実機検証済み)** |
| 9 | Control Mapping ヘルパー | `unit/control_mapping`、`manual/control_mapping.ja.md` | **完了** |

基盤ライブラリへの変更依頼は Phase 0 の時点で提案済みです([LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md))。Phase 1〜5 は依頼と無関係に進むので、対応を待つ時間は発生しません。

## Phase ごとの残作業

### Phase 1: 共通表現(完了)

- ✅ `espmidi::Message` / `PortId` / `Timestamp` / `MessageType` の定義
- ✅ MIDI 1.0 バイトストリーム → `Message` のパーサ。running status の解決、データ長の判定、SysEx 境界の検出とチャンク化
- ✅ `Message` → MIDI 1.0 バイト列のシリアライザ(`serializeShortMessage()`)
- ✅ USB MIDI イベントパケット(4 バイト、cable + CIN)⇄ `Message` のコーデック。USB Device と USB Host の両ポートで共用する
- ✅ ポインタ寿命規約(`raw` / `chunkData` はコールバック中のみ有効)。SysEx チャンクは入力バッファを直接指すので、音色ダンプがコピーなしで通る

実装で確定した細部:

- **SysEx チャンクは「入力バッファ内の連続した並び」単位**で切る。real-time バイトがダンプの途中に割り込むとチャンクが分かれるが、ストリームは終わらない(`chunkEnd` は立たない)。連続でなければコピーなしで渡せないため。
- **0xF7 以外のステータスバイトも SysEx を終わらせる。** 打ち切られたダンプでも `chunkEnd` を立てるので、下流のポートが送信中のストリームを閉じられる(ROUTING の規則 2)。
- **USB の CIN 0x5 は byte1 が 0xF7 かどうかで判別する。** 「SysEx が 1 バイトで終わる」と「単バイト System Common」の兼用 CIN で、SysEx を終わらせられるのは 0xF7 だけなので曖昧さはない。
- **USB の SysEx 状態は cable ごとに持つ。** cable は独立したポートで、1 回のバルク転送に複数 cable のパケットが混ざるため。
- **開始を見ていない SysEx 継続パケットは捨てる。** 途中でリセットした場合に「始まりのないチャンク」を作らないため。

### Phase 2: ポートモデル(完了)

- ✅ Endpoint / InPort / OutPort と不透明ハンドル、自動採番
- ✅ 席モデル。切断でハンドルを無効化せず状態だけ変える
- ✅ 識別子による再接続時の席の照合
- ✅ 静的ポートと動的ポートの供給インターフェース(アダプタは「ポートの供給者」)
- ✅ メタデータ(transport 種別 / 名前 / 識別子 / 方向 / 状態)
- ✅ ポート群と、すべての入力 / すべての出力の暗黙の群
- ✅ ポートの追加・状態変化の通知

実装で確定した細部:

- **ポートは削除されない。** 席は機器より長生きするので `PortRemoved` イベントは存在せず、消費側は「席が現れた」と「席の状態が変わった」の 2 つだけを扱う。
- **`attachEndpoint()` / `attachInPort()` は冪等。** 再接続の処理は接続処理をもう一度走らせるだけで済み、同じ席が返る。
- **状態はエンドポイント単位で動く。** 出入りするのは接続であって個々の cable ではないので、`detachEndpoint()` が配下の全ポートを切断中にする。
- **識別できない機器は毎回新しい席。** シリアルを持たない USB 機器で席を推測すると、別の機器に前の機器のルーティングを渡してしまう。
- **通知は関数ポインタ + コンテキスト。** キャプチャ付き `std::function` はヒープを使い、この通知は機器の列挙中にトランスポートのタスクから走るため。
- **記憶域は固定長。** `ESPMIDI_MAX_ENDPOINTS`(既定 8)/ `ESPMIDI_MAX_PORTS`(既定 32)/ `ESPMIDI_MAX_PORT_GROUPS`(既定 8)で調整でき、UART 2 本を繋ぐスケッチが USB 機器 16 台分を払わない。

### Phase 3: ルーティングと駆動(完了)

- ✅ `Route` と、端点にポートまたはポート群を許す `addRoute()`
- ✅ 3 段パイプライン(入力ポート前処理 → ルート → 出力ポート後処理)。各段にユーザーコードのコールバックを置ける
- ✅ ルートの登録順による決定的な処理。ルート間は独立
- ✅ キューとコピー、`update()` による排出、あふれ時の破棄とカウンタ
- ✅ SysEx 規則 1(経路は開始時確定)
- ✅ SysEx 規則 2(切断時は `0xF7` で閉じて破棄)
- ✅ SysEx 規則 3(出力ポートの SysEx 排他。System Real-Time のみ割り込み可)
- ✅ ループ防止。同一エンドポイント既定禁止とルート単位の解除
- ✅ 診断カウンタ 8 種
- ✅ **アプリケーションポート**(決定 6)

実装で確定した細部:

- **静的循環検査は作らなかった。** ルーティングは In → Out の一方向で内部に Out → In の辺が無く、検査対象が存在しないため([DECISIONS.ja.md](DECISIONS.ja.md) の決定 4)。
- **SysEx 送信中に送れない通常メッセージは捨ててカウント**(当初の「待たせる」から変更)。待たせるには出力ごとの遅延バッファが要る。
- **チャンクは段のコールバックを通らない。** 規則 1 との衝突を避けるため。
- **キューのエントリを超えるチャンクは分割する。** `chunkStart` / `chunkEnd` は最初と最後の断片だけが持つので 1 本のストリームのまま。
- **`update()` が処理するのは呼び出し時点の分だけ。** 段から注入されたメッセージは次の `update()` に回るので、アプリのフィードバックが 1 回の `update()` を無限に回さない。
- **記憶域は固定長。** `ESPMIDI_MAX_ROUTES`(既定 16)/ `ESPMIDI_QUEUE_ENTRIES`(既定 32)/ `ESPMIDI_CHUNK_BYTES`(既定 48)。キューは約 2KB。

### Phase 4: フィルタと変換(完了)

- ✅ フィルタ条件(メッセージ種別 / チャンネル / ノート範囲 / Control Change 番号)。送信元ポートはルートの端点そのものなので条件に含めない
- ✅ 変換(チャンネル設定とオフセット / トランスポーズとノートオフセット / Velocity / Control Change 番号と値 / プレッシャー)
- ✅ **値の幅を後から広げられる API 形状の確定**([DECISIONS.ja.md](DECISIONS.ja.md) の仮置き 1)

実装で確定した細部:

- **段は 3 種類の規則を持つ**(フィルタ → 変換 → コールバックの順)。ルートにも入力ポートにも出力ポートにも同じ形で置ける。
- **`ValueMap` は端点を正規化して保持する。** `range7()` / `scale7()` / `fixed7()` は「7 bit 単位で書いた」ことを表すだけで、規則自体は幅を持たない。MIDI 2.0 では `range16()` を足すだけになる。
- **変換に付け替え表を持たない。** 「CC7 を CC11 へ」はフィルタで絞って変換で番号を設定する組み合わせで書ける。
- **Velocity 0 のノートオンは触らない。** ワイヤ上はノートオフなので、スケールすると「止まらない小さな音」になる。
- **チャンネルプレッシャーは値が第 1 バイト**にある。ポリフォニックプレッシャーと同じ `pressure` で扱うが、書き込む位置が違う。
- **トランスポートで範囲外に出たノートは捨てる。** 折り返すと鍵盤の反対端で鳴る。
- **フィルタが最初のチャンクを弾いた出力はストリームを掴まない。** 掴むと、送っていないストリームで出力が塞がったままになる。

### Phase 5: UART ポート(完了・実機検証済み)

- ✅ `EspMidiUart.h`。31250 baud、送受信、SysEx の枠付け
- ✅ `espmidi::Serializer`(送信側のコーデック)。`Parser` の対になる半分として `EspMidiParser.h` へ
- ✅ `loopback/uart_midi`。GPIO マトリクスで UART1_TX と UART2_RX を同一 GPIO に割り当てる配線ゼロ構成
- ✅ `peer/uart_midi`。既存の peer 配線を役割ごとの TX / RX 入れ替えでクロスとして使う
- ✅ `examples/UartMidiMonitor`

実装で確定した細部:

- **ポートはシリアルオブジェクトのテンプレート**(`BasicUartPort<T>`、`UartPort` は `HardwareSerial` 版の別名)。ポートの挙動をホスト上で固定でき、**実機のテストに残るのはバイトがパッドを渡ることだけ**になる。
- **送信に running status を使わない。** 1 バイト在庫が減るより、1 つの出力に複数入力のメッセージが乗ることと、圧縮したストリームは 1 バイト落ちると以降すべてが読み違いになることのほうが重い。
- **枠付けは `Serializer` が持つ。** チャンクが運ぶのはペイロードだけなので、`0xF0` と `0xF7` は送信側で付ける。始まりを見ていない継続チャンクは拒否する(裸のデータバイトを線に出さないため)。
- **`end()` は送信途中のストリームを `0xF7` で閉じる。** 相手が中途半端なダンプを抱えたまま待ち続けないため(規則 2)。
- **1 回の `update()` で読むバイト数に上限がある**(`ESPMIDI_UART_RX_BYTES`、既定 64)。ダンプを流す機器が `loop()` を占有しない。
- **配線ゼロのループバックには GPIO マトリクスへの直接接続が要る。** Arduino のピン管理は 1 ピンにつき 1 ペリフェラルしか覚えず、2 つ目が要求すると 1 つ目を外してしまうので、受信側を先に張ってから送信信号を `esp_rom_gpio_connect_out_signal()` で重ねる。**実機で動作を確認した。**
- **実機テストのスケッチは `setup()` で結果を出してはいけない。** 書き込みツールがボードをリセットし、コンソールはその後に開かれるので、起動直後の出力は誰も聞いていないうちに終わる。準備完了は**繰り返し**告げ、本番はホストに促されてから走らせる。
- **USB を使わないプロファイルはボード指定を `esp32:esp32:esp32s3` だけにする。** `USBMode` を指定する理由が無い。

### Phase 6: USB Device ポート(完了・実機検証済み)

- ✅ `EspMidiEspUsbDevice.h`。生パケット API(`readPacket` / `writePacket`)に乗る
- ✅ `EspUsbDeviceMidi(device, inCableCount, outCableCount)` で宣言した cable 数だけポートを供給する
- ✅ cable 範囲外を拒否する
- ✅ `peer/usb_midi`。**cable 数そのものを `getMidiPortInfo()` で assert してから**往復を確認する([../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) の「cable のテストで気をつけること」)
- ✅ `examples/UsbMidiDevice`

実装で確定した細部:

- **cable 数の向きは反転する。** `inCableCount()` は device → host なので**出力**ポート、`outCableCount()` は host → device なので**入力**ポートになる。ポート側の数え方はこのライブラリの向きに統一した(`inPortCount()` / `outPortCount()`)。
- **対称な cable 構成では取り違えを検出できない。** 受信メッセージの cable 番号は自分のヘッダから読んだ値なので、往復しても正しく見える。だから peer テストの機器は **2 / 3 の非対称**にした。
- **cable 数 0 はポート 1 本ではない。** その方向のポートを作らない。
- **席が使えるかどうかはホストが決める。** `ready()`(= `tud_mounted()`)を `update()` が追い、mount / unmount で状態だけが動く。**席は消えない**ので抜き差しでルートを張り直さない。
- **unmount 中のストリームは捨てる。** `0xF7` を送る相手がもういない。次のダンプが打ち切られたダンプの続きにならないよう、エンコーダを畳む。
- **宣言されていない cable のパケットは捨てて数える**(`unknownCablePackets()`)。捨てなければ別の席に載る。
- **peer テストの descriptor は小さく保つ。** ESP-IDF の USB Host は列挙時の control transfer より長い configuration descriptor を拒否し、Arduino のプリコンパイル済みライブラリではその上限が 256 バイト。16 cable は正当な USB で PC 相手には動くが、この試験機では列挙できない。

### Phase 7: USB Host ポート(完了・実機検証済み)

- ✅ `EspMidiEspUsbHost.h`。MIDI リスナと `midiSend` に乗る
- ✅ 動的なエンドポイントの供給。接続・切断への追従、識別子による席の照合
- ✅ SysEx の連結(`EspUsbHost` が持たないので core のコーデックを使う)
- ✅ `getMidiPortInfo()` でポート数を確定させる
- ✅ **キューを本当にスレッドセーフにした**(下記)
- ✅ `peer/usb_midi_host`、`examples/UsbHostToUart`

実装で確定した細部:

- **`Router::receive()` を実際にスレッドセーフにした。** [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) が最初から約束していたのに、キューは `head_` と `count_` を両側から書く形で成立していなかった。**USB Host はライブラリのタスクからコールバックが来る最初のポート**なので、ここで直した。ロックフリーの MPSC リング(CAS で席を取り、エントリごとの公開フラグで渡す)にし、`unit/concurrent_receive` が 4 スレッド × 2000 通で「受理したものは 1 度ずつ出てくる」「失ったものは数えられる」を固定している。**修正前のキューは同じテストで数百通を静かに失っていた。**
- **`counters()` は参照ではなくスナップショットを返す。** `received` と `queueFull` はトランスポートのタスクから書かれるので、まとめて読んで値で渡す。
- **席に触るコードは 1 つのタスクに閉じ込めた。** ライブラリのコールバックでやるのは**生パケット 4 バイトをロックフリーのリングに写すこと**だけ。デコーダも cable の対応もレジストリも `update()` からしか触らない。
- **接続は callback ではなく polling で見つける**(`getDevices()` を 100 ms ごと)。同じ理由で、発見もレジストリを持つ側に置いた。列挙のほうがずっと遅いので機器が遅れて見えることはない。
- **機器は列挙されてもすぐ MIDI とは分からない。** `getMidiPortInfo()` は claim 前だと失敗するので、失敗したら次の polling で見直す。諦めると「一瞬遅れた鍵盤」に席が付かない。
- **cable 数の向きは反転しない。** `EspUsbHost` の数え方は既にホスト視点で、このライブラリがそのホストだから。**USB Device ポートとは逆**になる。
- **席の照合はアドレスではなく識別子で行う。** アドレスはその回にスタックが配っただけの番号。
- **切断した席への送信は失敗させる。** 次に同じアドレスを取った別の機器へ届いてしまうため。
- **切断が SysEx の途中なら、規則 2 が下流のストリームを閉じる。** これは実装ではなく既存のルーティング規則がそのまま効く場面で、テストで固定した。
- **リングは 2 段目のキューになる。** ポートのリング(既定 64)とルータのキュー(既定 32)の 2 つがあり、溢れはそれぞれ `droppedPackets()` と `queueFull` に出る。どちらで落ちたかで「読むのが遅い」と「流すのが遅い」を区別できる。

### Phase 8: BLE ポート(完了・実機検証済み)

- ✅ `EspMidiEspBle.h`。`EspBleMidiDevice` / `EspBleMidiHost` に乗る
- ✅ `EspBleMidiMessage` → `espmidi::Message` の変換。タイムスタンプは `Milliseconds13`
- ✅ Host 側の動的エンドポイント供給。BLE アドレスによる席の照合
- ✅ `peer/ble_midi`、`examples/BleMidiToUart`

実装で確定した細部:

- **`Transport::Ble` を `BleDevice` と `BleHost` に分けた。** USB と同じ理由で、**Device 側の席は静的、Host 側は動的**だから。分ける前は Device 側の識別子が「識別できない」と判定され、購読するたびに新しい席ができていた。**`unit/ble_port` の最初の実行がそれを捕まえた。**
- **ダンプの再組み立てはこのポートだけが持つ。** `EspBle` は `0xF0..0xF7` の完全なメッセージを受け取って自分で分割するので、チャンクをここで繋ぎ直す。上限は `ESPMIDI_BLE_SYSEX_BYTES`(既定 320 = `EspBle` 側の上限)で、**超えたダンプは切り詰めずに拒否して数える**。中途半端なダンプは送らないほうがまし。
- **受信は BLE タスクからそのままキューへ入れる。** Phase 7 で `receive()` を本当にスレッドセーフにしたので、パケットを写し取るリングを挟まなくてよくなった。おかげで**ダンプがコピーされない**(チャンクは NimBLE のバッファを指したまま)。共有するのは接続ごとのポートハンドル 1 語だけ。
- **接続の通知だけはリング越し。** BLE には USB の `getDevices()` にあたる列挙がないので接続は listener で来る。中身は接続 ID だけなので小さい。
- **接続 ID 0 は実在する。** 公開用のスロットを 0 で初期化すると空きスロットが「接続 0」に見えるので、`0xffff` で初期化する。
- **`discover()` はポートが呼ぶ。** スキャンと接続はスケッチ、接続の中身はポート。席ができるのは**サービスの発見と購読が終わったとき**で、リンクが繋がった時点ではない。
- **MIDI のコールバックは `EspMidi` が取る。** `EspBle` 側はどちらも 1 個だけの primary callback なので、スケッチは**ルーティング経由で読む**。接続の通知は additional listener なのでスケッチの `onConnected()` は奪わない。
- **`EspBle` の名前が Device と Host で違う**(`onMessage()` / `onMidiMessage()`)。呼び分けている。

### Phase 9: Control Mapping ヘルパー(完了)

- ✅ ボタン → Note / Control Change(デバウンス、ラッチ)
- ✅ アナログ入力 → Control Change(範囲、反転、ヒステリシス、スムージング)
- ✅ エンコーダ → Control Change(絶対と相対 3 形式)
- ✅ Note 受信による LED 制御、Control Change 受信による出力制御(`ControlOutput`)
- ✅ 外部クロックと MIDI Clock の変換(`ClockCounter` で測って `ClockGenerator` で出す)
- ✅ 入力元を GPIO 番号に固定しない形
- ✅ `examples/GpioControls`、`manual/control_mapping.ja.md`

実装で確定した細部:

- **GPIO に触らない。時刻も読まない。** ヘルパーは**読んだ値と今の時刻を引数で受け取る**。だから ADC でもポートエキスパンダでもタッチセンサでもネットワーク越しの値でも同じヘルパーが動き、**跳ねるスイッチもテンポ変化もホスト上のテストで作れる**。`analogRead()` を持つヘルパーは 1 種類の入力にしか使えないヘルパーになる。
- **core に置いた**(ポートではない)。ハードウェアに触らないので `EspMidi.h` から include している。
- **最初の読み取りは何も送らない。** ペダルを踏んだまま起動したボードが、誰も弾いていないノートを報告しないため。明示的に送りたいときは `resend()`。
- **つまみは値が変わったときだけ送る。** ADC 4096 段に対して CC は 128 段なので、ほとんどの動きは同じ値に着地する。加えてヒステリシス(既定 8 カウント)で、**触っていないつまみがリンクを埋めない**。
- **エンコーダの相対形式は 3 種類とも実装した。** 標準が無く、間違えるとパラメータが逆に回るか動かない。**63 を超える回転は複数メッセージに分ける**(clamp すると速い回転が失われる)。
- **`ControlOutput` は `Filter` を再利用する。** 「ch1 のノート 60」も「CC 20〜27」も、ルートに置くフィルタと同じ書き方になる。**ノートオンでベロシティ 0 は消灯**なので LED が点いたままにならない。
- **クロックの追いつきには上限がある。** 長い SysEx や詰まった書き込みで `loop()` が止まったあと、遅れを全部出すと音楽的に無意味なバーストになる。1 回の `update()` で 24 tick までにして、あとはスケジュールを取り直す。
- **テンポは BPM の 100 倍の整数**で扱う。core に浮動小数点の要求を作らず、0.01 BPM は聴感より細かい。

## examples

各 Phase でポートが動くようになった時点で追加します。examples は**すべて実用例**とし、そのまま書き込めるスケッチにします([../examples/README.ja.md](../examples/README.ja.md))。

候補は [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) の想定利用例に対応させます。

| example 候補 | 必要な Phase |
| --- | --- |
| ~~UART MIDI のモニタ~~ → `UartMidiMonitor` | 5 |
| ~~USB Device MIDI(PC から見える最小構成)~~ → `UsbMidiDevice` | 5, 6 |
| ~~USB Host の演奏を UART の音源へ~~ → `UsbHostToUart` に含む | 5, 7 |
| ~~USB Host の演奏を PC と外部音源へ同時に(UC1)~~ → `UsbHostToUart` | 5, 6, 7 |
| 複数機器を集約して PC の複数ポートに(UC2) | 6, 7 |
| ~~BLE MIDI キーボードを UART 音源へ中継(UC5)~~ → `BleMidiToUart` | 5, 8 |
| MIDI と HID の同居(UC6) | 6 |
| チャンネルの振り分けとトランスポーズ(UC7) | 4, 5, 7 |
| ~~GPIO のつまみを Control Change に(UC10)~~ → `GpioControls` | 6, 9 |

## 未確定事項

**ありません。** [DECISIONS.ja.md](DECISIONS.ja.md) の「仮置き」3 件はすべて解消しました。

1. ~~フィルタと変換の具体的な API 形状~~ → Phase 4 で確定
2. ~~キューの深さと SysEx 一時バッファのサイズの既定値~~ → Phase 3 で確定
3. ~~暗黙のポート群をハンドルとして露出するか~~ → **予約ハンドルとして露出する**(`InGroup::all()` / `OutGroup::all()`)。Phase 2 で確定

## ここから先

実装計画は完了しました。次にやることの候補です。

- **リリース。** [RELEASE_CHECKLIST.ja.md](RELEASE_CHECKLIST.ja.md) の手順で 0.1.0 を出す。
- **`manual/` の手順を足す。** [../tests/manual/README.ja.md](../tests/manual/README.ja.md) の「追加予定」に 5 件残っている(実 MIDI DIN、Host OS の認識、実 USB MIDI 機器、BLE ペアリング、実機の音色ダンプ)。**実機と人の目が要るので自動化しない**と決めた分です。
- **`loopback/usb_host_device`**(ESP32-P4 1 台で USB Host と USB Device を同時に)。`examples/UsbHostToUart` の構成をそのまま自動テストにできる。
- **MIDI 2.0 / UMP。** 器の側は地続きにしてあります([DECISIONS.ja.md](DECISIONS.ja.md) の決定 1)。`MessageType` は UMP のメッセージタイプ番号、`Timestamp` は単位付き、`ValueMap` は端点を正規化して持つので、**幅を広げても既存の規則の意味が変わりません**。
