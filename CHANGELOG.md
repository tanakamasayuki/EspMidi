# Changelog

## Unreleased

- リポジトリの骨格を作成した。docs / tests / CI / リリース資産を配置し、`tests/unit/test_repository_structure.py` が必須ファイルの存在を検査する状態にした。
- `docs/REQUIREMENTS.ja.md` を要件定義として起こし、たたき台の `memo.ja.md` を削除した。
- 基本方針を `docs/DECISIONS.ja.md` の決定 1〜5 として確定した(共通表現 / 依存方向 / ポートモデル / ルーティング / テスト環境)。
- 共通表現とワイヤ形式のコーデックを実装した(Phase 1)。`espmidi::Message` / `PortId` / `Timestamp` / `MessageType`、MIDI 1.0 バイトストリームのパーサ(running status の解決、real-time の割り込み、SysEx のチャンク化)、USB MIDI イベントパケットのコーデック(cable ごとの SysEx 組み立てと分解)。SysEx チャンクは入力バッファを直接指すので、音色ダンプがコピーなしで通る。
- ポートモデルを実装した(Phase 2)。`espmidi::PortRegistry` が Endpoint > Port の 2 階層、切断でも失われない「席」としてのハンドル、識別子による再接続時の席の照合、状態のエンドポイント単位の伝播、メタデータ、ポート群(`InGroup::all()` / `OutGroup::all()` は予約ハンドル)、席の追加と状態変化の通知を持つ。記憶域は固定長で、`ESPMIDI_MAX_ENDPOINTS` などで調整できる。
- ルーティングを実装した(Phase 3)。`espmidi::Router` がルート(端点はポートまたはポート群)、3 段パイプライン、キュー駆動の `update()`、SysEx 3 規則、同一エンドポイントへ戻さないループ規則、診断カウンタ 8 種を持つ。各段にはユーザーコードのコールバックを置ける。
- **アプリケーションポート `espmidi::AppPort` を追加した**(決定 6)。トランスポートに裏打ちされないポートで、スケッチがメッセージを注入し受け取る。モニタも Control Mapping ヘルパーもこの上に乗り、アプリケーションは特別扱いではなくポートの 1 つとして扱われる。
- 仕様を 2 点訂正した。**多段の循環はルーティングが In → Out の一方向なので内部では起きない**ため、静的循環検査は作らなかった。**SysEx 送信中に送れない通常メッセージは「待たせる」から「捨ててカウント」へ変更**した(待たせるには出力ごとの遅延バッファが必要になるため)。
- 宣言的なフィルタと変換を実装した(Phase 4)。`espmidi::Filter`(種別 / チャンネル / ノート範囲 / CC 番号)と `espmidi::Transform`(チャンネル / トランスポーズ / Velocity / CC 番号と値 / プレッシャー)を、ルート・入力ポート・出力ポートのどの段にも置ける。適用順はフィルタ → 変換 → コールバック。値は `espmidi::ValueMap` が端点を正規化して保持するので、MIDI 2.0 で幅が広がっても規則の意味が変わらない。**これで core が完成し、ハードウェア非依存部分はすべてホスト上のテストで固定された。**
- **最初のポートとして UART MIDI を実装した**(Phase 5)。`espmidi::UartPort` が `HardwareSerial` を 31250 baud で開き、席を 1 組供給し、受信を router へ、送信をワイヤへ流す。送信側のコーデック `espmidi::Serializer` を `Parser` の対として追加した(SysEx の `0xF0` / `0xF7` の枠付け、送信途中のストリームを閉じる `closeStream()`)。送信に running status は使わない。
- **ポートをシリアルオブジェクトのテンプレートにした**(`BasicUartPort<T>`、`UartPort` はその `HardwareSerial` 版)。席の供給、受信の router 到達、枠付け、送信バッファ満杯、中断したダンプの終端まで**ホスト上のテストで固定**でき、実機のテストに残るのはバイトがパッドを渡ることだけになる。
- UART の実機テストを 2 つ追加し、**ESP32-S3 実機で確認した**。`loopback/uart_midi` は **配線ゼロ**(GPIO マトリクスで UART1_TX と UART2_RX を同一ピンに載せる)、`peer/uart_midi` は**既存の peer 配線をそのままクロスとして使う**。どちらも新しい配線を必要としない。
- 実機テストのスケッチは、準備完了を繰り返し告げてホストに促されてから走る形にした。書き込みツールがボードをリセットし、コンソールが開かれるのはその後なので、`setup()` で出した結果は誰も聞いていないうちに終わる。
- `examples/UartMidiMonitor` を追加した。監視をアプリケーションポートに置き、通し経路とは別のルートにすることで、表示が遅くても音が遅れない形にしてある。
- **USB Device MIDI ポートを実装した**(Phase 6)。`espmidi::UsbDevicePort` が `EspUsbDeviceMidi` の生パケット API に乗り、宣言された cable 数だけ席を供給する。ホストの mount / unmount を席の状態に反映し、宣言されていない cable のパケットは捨てて数える。
- **cable 数の向きの反転を API で防いだ。** `EspUsbDevice` の `inCableCount()` は device → host なのでこのライブラリの**出力**ポート、`outCableCount()` は**入力**ポートになる。ポート側は `inPortCount()` / `outPortCount()` とこのライブラリの向きで数える。**対称な cable 構成では取り違えを検出できない**ため、peer テストの機器は 2 / 3 の非対称にした。
- `peer/usb_midi` を追加し、実機で確認した。**cable 数そのものを `getMidiPortInfo()` で assert**してから往復を見る。DUT 側は素の `EspUsbHost` で、両端が同じコードでパケットを組んで誤りが打ち消し合うのを避けた。
- `examples/UsbMidiDevice` を追加した。PC に 2 ポートの USB MIDI インターフェースとして見え、ポート 1 が MIDI DIN 対、ポート 2 がボード自身になる。
- 兄弟ライブラリの依存を**公開バージョン**に固定した(`EspUsbHost` 2.7.5 / `EspUsbDevice` 2.0.2)。依頼 1・2 の cable 対応はどちらもリリース済みなので、`*_local` プロファイルは開発版を試すときだけ使う。
- **USB Host MIDI ポートを実装した**(Phase 7)。`espmidi::UsbHostPort` が接続された機器ごとに席を動的に供給する。ポート数は `getMidiPortInfo()` で確定させ、SysEx の連結は core のコーデックが行い、席の照合はアドレスではなく識別子(VID / PID / シリアル)で行う。**cable 数の向きは反転しない**(`EspUsbHost` の数え方が既にホスト視点だから)ので、USB Device ポートとはちょうど逆になる。
- **`Router::receive()` を実際にスレッドセーフにした。** `docs/CORE_DESIGN.ja.md` が最初から約束していたのに、キューは `head_` と `count_` を両側から書く形で成立していなかった。**USB Host はライブラリのタスクからコールバックが来る最初のポート**なので、ここで直した。ロックフリーの MPSC リング(CAS で席を取り、エントリごとの公開フラグで渡す)にし、スピンロックは使わない(高優先度のトランスポートタスクが `loop()` を待って回るのは単一コアだと危険なため)。`unit/concurrent_receive` が 4 スレッド × 2000 通で固定している。**修正前のキューは同じテストで数百通を静かに失っていた。**
- `Router::counters()` が参照ではなくスナップショットを返すようになった。`received` と `queueFull` はトランスポートのタスクから書かれるため。
- **席に触るコードを 1 つのタスクに閉じ込めた。** `EspUsbHost` のコールバックでやるのは生パケット 4 バイトをロックフリーのリングに写すことだけで、デコーダも cable の対応もレジストリも `update()` からしか触らない。接続の発見も callback ではなく `getDevices()` の polling にしたのは同じ理由。
- `peer/usb_midi_host` を追加し、実機で確認した。両端が `EspMidi` で、機器をどこにも書かずに**挿したときに現れた席**を `InGroup::all()` のルートが拾う。素の `EspUsbHost` を観測役にした `peer/usb_midi` は、device 側が独立した相手に対して証明されている状態を保つために残した。
- `examples/UsbHostToUart` を追加した(UC1)。USB MIDI キーボードで MIDI DIN の音源を鳴らしながら、同じ演奏を PC にも流す。
- **BLE MIDI ポートを実装した**(Phase 8)。`espmidi::BleDevicePort`(このボードが MIDI 機器)と `espmidi::BleHostPort`(このボードが BLE MIDI キーボードに繋ぐ)。**タイムスタンプを持つ唯一のポート**で、13 bit ミリ秒を `TimestampUnit::Milliseconds13` として素通しする。これで**同梱ポートが全部揃った。**
- **`Transport::Ble` を `BleDevice` と `BleHost` に分けた。** USB と同じ理由で、Device 側の席は静的、Host 側は動的だから。分ける前は Device 側の識別子が「識別できない」と判定され、購読するたびに新しい席ができていた。
- **ダンプの再組み立ては BLE ポートだけが持つ。** `EspBle` は `0xF0..0xF7` の完全なメッセージを受け取って自分で分割するので、ルーティングが渡すチャンクをここで繋ぎ直す。上限を超えたダンプは切り詰めずに拒否して数える(`oversizedStreams()`)。
- **BLE の受信は BLE タスクからそのままキューへ入れる。** Phase 7 で `receive()` を本当にスレッドセーフにしたので、パケットを写し取るリングを挟まなくてよくなり、**ダンプがコピーされない**。
- `peer/ble_midi` を追加し、実機で確認した(無線・ペアリングなし)。`examples/BleMidiToUart` を追加した(UC5)。
- **Control Mapping ヘルパーを実装した**(Phase 9)。`espmidi::Button`(デバウンス / ラッチ)、`Analog`(範囲 / 反転 / ヒステリシス / スムージング)、`Encoder`(絶対と相対 3 形式)、`ControlOutput`(受信 → LED などの制御)、`ClockGenerator` / `ClockCounter`(MIDI Clock の生成と計測)。**これで実装計画が完了した。**
- **ヘルパーは GPIO にも時刻にも触らない。** 読んだ値と今の時刻を引数で受け取るので、ADC でもポートエキスパンダでもタッチセンサでも同じものが使え、**跳ねるスイッチもテンポ変化もホスト上のテストで作れる**。ハードウェアに依存しないので core に置き `EspMidi.h` から include した。
- `examples/GpioControls`(UC10)と `tests/manual/control_mapping.ja.md` を追加した。人の手にどう感じられるかは assert できないので、そこだけを手順として切り出した。
- 基盤ライブラリへの変更依頼 2 件を提案した(`docs/LIBRARY_REQUESTS.ja.md`)。**両方の cable 数対応が実装された**ので、実装結果(API の形、提案の誤りの訂正、実装時に判明した注意点)を `docs/PORTS.ja.md` と `tests/TEST_PLAN.ja.md` へ反映した。cable 名は両側とも見送り。
