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
- 基盤ライブラリへの変更依頼 2 件を提案した(`docs/LIBRARY_REQUESTS.ja.md`)。**両方の cable 数対応が実装された**ので、実装結果(API の形、提案の誤りの訂正、実装時に判明した注意点)を `docs/PORTS.ja.md` と `tests/TEST_PLAN.ja.md` へ反映した。cable 名は両側とも見送り。
