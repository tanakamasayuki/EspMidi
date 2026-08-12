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
- 基盤ライブラリへの変更依頼 2 件を提案した(`docs/LIBRARY_REQUESTS.ja.md`)。**両方の cable 数対応が実装された**ので、実装結果(API の形、提案の誤りの訂正、実装時に判明した注意点)を `docs/PORTS.ja.md` と `tests/TEST_PLAN.ja.md` へ反映した。cable 名は両側とも見送り。
