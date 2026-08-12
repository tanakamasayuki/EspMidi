# 開発計画

実装順、現在地、残作業です。設計の正本は [DATA_MODEL.ja.md](DATA_MODEL.ja.md) / [ROUTING.ja.md](ROUTING.ja.md) / [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)、判断の理由は [DECISIONS.ja.md](DECISIONS.ja.md) です。

## 現在地

**Phase 3 完了。** 共通表現、ワイヤ形式のコーデック、ポートモデル、ルーティングが揃い、すべてホスト上のテストで固定されています。

- `src/EspMidiMessage.h` — `Message` / `PortId` / `Timestamp` / `MessageType`、ステータス分類とデータ長表、短いメッセージの構築と直列化
- `src/EspMidiParser.h` — MIDI 1.0 バイトストリーム → `Message`(UART 用)
- `src/EspMidiUsbPacket.h` — USB MIDI イベントパケット ⇄ `Message`(USB Host / Device 共用)
- `src/EspMidiPort.h` — `PortRegistry`。Endpoint / InPort / OutPort、席モデル、状態、メタデータ、ポート群、通知
- `src/EspMidiRouter.h` — `Router` と `AppPort`。ルート、3 段パイプライン、キュー駆動、SysEx 3 規則、ループ規則、診断カウンタ

**この時点で core は動きます。** ポートを供給してシンクを登録すれば、実機なしでも `AppPort` 同士で end-to-end の転送が回ります。残るのは宣言的なフィルタ / 変換(Phase 4)と、実際のトランスポートに繋ぐポート(Phase 5 以降)です。

## 実装順

Phase 1〜4 はすべてホスト上で完結するので、実機なしで core が完成します。

| Phase | 内容 | テスト | 状況 |
| --- | --- | --- | --- |
| 0 | リポジトリ骨格、docs、テスト環境、CI | `unit/test_repository_structure.py`、`arduino_smoke/` | **完了** |
| 1 | 共通表現。`Message` / `PortId` / `Timestamp` / `MessageType` と MIDI 1.0 バイトストリームのパーサ(running status、SysEx チャンク)、USB パケットコーデック | `unit/message`、`unit/parser`、`unit/usb_packet` | **完了** |
| 2 | ポートモデル。Endpoint / Port / 席 / 状態 / メタデータ / ポート群 | `unit/port_model` | **完了** |
| 3 | ルーティングと駆動。Route / 3 段パイプライン / キュー / SysEx 3 規則 / アプリケーションポート | `unit/routing`、`unit/sysex_rules` | **完了** |
| 4 | フィルタと変換 | `unit/filter`、`unit/transform` | 予定 |
| 5 | UART ポート | `loopback/uart_midi`(配線ゼロ) | 予定 |
| 6 | USB Device ポート | `peer/usb_midi` | 予定([依頼 1](LIBRARY_REQUESTS.ja.md) の cable 数は実装済み) |
| 7 | USB Host ポート | `peer/usb_midi` | 予定([依頼 2](LIBRARY_REQUESTS.ja.md) の cable 数は実装済み) |
| 8 | BLE ポート(Device / Host) | `peer/ble_midi` | 予定 |
| 9 | Control Mapping ヘルパー | `unit/control_mapping`、`manual/` | 予定 |

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

### Phase 4: フィルタと変換

- フィルタ条件(メッセージ種別 / チャンネル / ノート範囲 / Control Change 番号 / 送信元ポート)
- 変換(チャンネル変更 / ノート番号変更 / トランスポーズ / Velocity 変更 / Control Change 番号変更 / Control Change 値の範囲変換)
- **値の幅を後から広げられる API 形状の確定**([DECISIONS.ja.md](DECISIONS.ja.md) の仮置き 1)

### Phase 5: UART ポート

- `EspMidiUart.h`。31250 baud、送受信、SysEx のチャンク化
- `loopback/uart_midi`。GPIO マトリクスで UART1_TX と UART2_RX を同一 GPIO に割り当てる配線ゼロ構成
- `peer/uart_midi`。既存の peer 配線を役割ごとの TX / RX 入れ替えでクロスとして使う

### Phase 6: USB Device ポート

- `EspMidiEspUsbDevice.h`。生パケット API に乗る
- `EspUsbDeviceMidi(device, cableCount)` で宣言した cable 数だけポートを供給する
- cable 範囲外への送信を拒否する(`EspUsbDevice` の helper と同じ方針)
- 複合構成では descriptor サイズに注意する。`descriptorLength()` で事前に分かる([LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md) の依頼 1)
- `peer/usb_midi`。**cable 数そのものを `getMidiPortInfo()` で assert してから**往復を確認する([../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) の「cable のテストで気をつけること」)

### Phase 7: USB Host ポート

- `EspMidiEspUsbHost.h`。MIDI リスナと `midiSend` に乗る
- 動的なエンドポイントの供給。接続・切断への追従、識別子による席の照合
- SysEx の連結(`EspUsbHost` が持たないので core のコーデックを使う)
- `getMidiPortInfo()` でポート数を確定させる。方向の反転、1 機器 = 1 エンドポイントの制約、cable 数 0 の扱いに注意([LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md) の依頼 2)

### Phase 8: BLE ポート

- `EspMidiEspBle.h`。`EspBleMidiDevice` / `EspBleMidiHost` に乗る
- `EspBleMidiMessage` → `espmidi::Message` の変換。タイムスタンプは `Milliseconds13`
- Host 側の動的エンドポイント供給。BLE アドレスによる席の照合
- `peer/ble_midi`

### Phase 9: Control Mapping ヘルパー

- ボタン → Note / Control Change
- アナログ入力 → Control Change
- エンコーダ → Control Change
- Note 受信による LED 制御、Control Change 受信による出力制御
- 外部クロックと MIDI Clock の変換
- 入力元を GPIO 番号に固定しない形

## examples

各 Phase でポートが動くようになった時点で追加します。examples は**すべて実用例**とし、そのまま書き込めるスケッチにします([../examples/README.ja.md](../examples/README.ja.md))。

候補は [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) の想定利用例に対応させます。

| example 候補 | 必要な Phase |
| --- | --- |
| UART MIDI のモニタ | 5 |
| USB Device MIDI(PC から見える最小構成) | 6 |
| USB Host の演奏を UART の音源へ | 5, 7 |
| USB Host の演奏を PC と外部音源へ同時に(UC1) | 5, 6, 7 |
| 複数機器を集約して PC の複数ポートに(UC2) | 6, 7 |
| BLE MIDI キーボードを UART 音源へ中継(UC5) | 5, 8 |
| MIDI と HID の同居(UC6) | 6 |
| チャンネルの振り分けとトランスポーズ(UC7) | 4, 5, 7 |
| GPIO のつまみを Control Change に(UC10) | 9 |

## 未確定事項

[DECISIONS.ja.md](DECISIONS.ja.md) の「仮置き」に挙げた 3 件です。

1. フィルタと変換の具体的な API 形状(Phase 4 で確定)
2. キューの深さと SysEx 一時バッファのサイズの既定値(Phase 3 で確定)
3. ~~暗黙のポート群をハンドルとして露出するか~~ → **予約ハンドルとして露出する**(`InGroup::all()` / `OutGroup::all()`)。Phase 2 で確定
