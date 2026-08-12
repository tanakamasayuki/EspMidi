# Changelog

## Unreleased

- リポジトリの骨格を作成した。docs / tests / CI / リリース資産を配置し、`tests/unit/test_repository_structure.py` が必須ファイルの存在を検査する状態にした。
- `docs/REQUIREMENTS.ja.md` を要件定義として起こし、たたき台の `memo.ja.md` を削除した。
- 基本方針を `docs/DECISIONS.ja.md` の決定 1〜5 として確定した(共通表現 / 依存方向 / ポートモデル / ルーティング / テスト環境)。
- 共通表現とワイヤ形式のコーデックを実装した(Phase 1)。`espmidi::Message` / `PortId` / `Timestamp` / `MessageType`、MIDI 1.0 バイトストリームのパーサ(running status の解決、real-time の割り込み、SysEx のチャンク化)、USB MIDI イベントパケットのコーデック(cable ごとの SysEx 組み立てと分解)。SysEx チャンクは入力バッファを直接指すので、音色ダンプがコピーなしで通る。
- ポートモデルを実装した(Phase 2)。`espmidi::PortRegistry` が Endpoint > Port の 2 階層、切断でも失われない「席」としてのハンドル、識別子による再接続時の席の照合、状態のエンドポイント単位の伝播、メタデータ、ポート群(`InGroup::all()` / `OutGroup::all()` は予約ハンドル)、席の追加と状態変化の通知を持つ。記憶域は固定長で、`ESPMIDI_MAX_ENDPOINTS` などで調整できる。
- 基盤ライブラリへの変更依頼 2 件を提案した(`docs/LIBRARY_REQUESTS.ja.md`)。**両方の cable 数対応が実装された**ので、実装結果(API の形、提案の誤りの訂正、実装時に判明した注意点)を `docs/PORTS.ja.md` と `tests/TEST_PLAN.ja.md` へ反映した。cable 名は両側とも見送り。
