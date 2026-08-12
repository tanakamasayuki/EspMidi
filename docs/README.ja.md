# ドキュメント案内

[English](README.md)

どの文書を、どの順で読むかの案内です。

**日英併記の範囲は 3 段に分けています。**

| 区分 | 言語 | 対象 |
| --- | --- | --- |
| 使う人が読むもの | **日英** | README、[GUIDE](GUIDE.ja.md)、[MIDI_BASICS](MIDI_BASICS.ja.md)、[RECIPES](RECIPES.ja.md)、[API](API.ja.md)、[FOOTPRINT](FOOTPRINT.ja.md)、[PORT_AUTHORING](PORT_AUTHORING.ja.md)、example、リリースチェックリスト |
| **確定した仕様** | **日英** | [DATA_MODEL](DATA_MODEL.ja.md)、[ROUTING](ROUTING.ja.md)、[PORTS](PORTS.ja.md) |
| 内部の記録・作業メモ | 日本語のみ | [REQUIREMENTS](REQUIREMENTS.ja.md)、[USE_CASES](USE_CASES.ja.md)、[CORE_DESIGN](CORE_DESIGN.ja.md)、[CONFIGURATION](CONFIGURATION.ja.md)、[DECISIONS](DECISIONS.ja.md)、[DEVELOPMENT_PLAN](DEVELOPMENT_PLAN.ja.md)、[LIBRARY_REQUESTS](LIBRARY_REQUESTS.ja.md)、テスト計画 |

**正本は日本語版です。** 英語版は日本語版に追随させます。

## まずここから

| やりたいこと | 読む文書 |
| --- | --- |
| ライブラリが何をするものか知り、動くスケッチを見る | [../README.ja.md](../README.ja.md) |
| **はじめて使う。1 ポートから順に覚える** | **[GUIDE.ja.md](GUIDE.ja.md)** → [../examples/SimpleMidiOut/](../examples/SimpleMidiOut/) |
| **やりたいことから引く** | **[RECIPES.ja.md](RECIPES.ja.md)** |
| **名前と引数を引く** | **[API.ja.md](API.ja.md)** |
| RAM をどれだけ使うか、削り方 | [FOOTPRINT.ja.md](FOOTPRINT.ja.md) |
| **MIDI 自体でハマった。インターフェース別の注意点を知る** | **[MIDI_BASICS.ja.md](MIDI_BASICS.ja.md)** |
| 動かない原因を切り分ける | [GUIDE.ja.md](GUIDE.ja.md) の「よくあるつまずき」 |
| 自分の構成向けのスケッチを探す | [../examples/README.ja.md](../examples/README.ja.md) — すべてそのまま書き込める完結したスケッチ |
| 使うポートを選び、できること・できないことを知る | [PORTS.ja.md](PORTS.ja.md) |
| MIDI メッセージとポートがなぜこの形なのかを知る | [DATA_MODEL.ja.md](DATA_MODEL.ja.md) |
| ルーティングの規則を知る(SysEx やループ防止を含む) | [ROUTING.ja.md](ROUTING.ja.md) |
| **自作ポートを書く** | **[PORT_AUTHORING.ja.md](PORT_AUTHORING.ja.md)** → `src/EspMidiUart.h` |
| 開発に参加する | [../CONTRIBUTING.ja.md](../CONTRIBUTING.ja.md) |
| 現在地と残作業を知る | [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) と [PORTS.ja.md](PORTS.ja.md) の状況列 |
| なぜそう設計したのかを知る | [DECISIONS.ja.md](DECISIONS.ja.md) |
| MIDI 2.0 対応の見通しを知る | [DECISIONS.ja.md](DECISIONS.ja.md) の決定 1 と [DATA_MODEL.ja.md](DATA_MODEL.ja.md) の「MIDI 2.0 への地続き」 |

## 文書一覧

**使う人向け**

- [GUIDE.ja.md](GUIDE.ja.md) — 使い方ガイド。1 ポートの送信から複数ポートのルーティングまで順に進みます。「よくあるつまずき」と診断カウンタの読み方もここ。
- [MIDI_BASICS.ja.md](MIDI_BASICS.ja.md) — MIDI 自体の注意事項(velocity 0、running status、チャンネルの 0 始まり、帯域)とインターフェース別の注意点(DIN の絶縁、cable、列挙、BLE のレイテンシと上限)。
- [RECIPES.ja.md](RECIPES.ja.md) — 「これをやりたい」から引く断片集。**コードは全部コンパイルされています**。
- [API.ja.md](API.ja.md) — 公開 API の一覧。名前と意味だけを引く場所。
- [FOOTPRINT.ja.md](FOOTPRINT.ja.md) — RAM と遅延の**実測値**。どこを削れるか。
- [PORT_AUTHORING.ja.md](PORT_AUTHORING.ja.md) — 自作ポートの契約。**ポートはこのリポジトリの外にも書けます**。

**設計(全体像を掴むならこの順)**

1. [REQUIREMENTS.ja.md](REQUIREMENTS.ja.md) — 何のためのライブラリで、どこまでを対象にするか。非目標も含む。
2. [USE_CASES.ja.md](USE_CASES.ja.md) — 設計検証に使った具体的なシナリオ。各ユースケースが確定させた規則も記録している(例: 音色ダンプの転送 → SysEx の経路は開始時に確定する)。
3. [DATA_MODEL.ja.md](DATA_MODEL.ja.md) — 中間表現とポートモデル。MIDI 1.0 バイト列を正準としつつ UMP へ地続きにした形、Endpoint > Port の 2 階層、論理的な席としてのハンドル。
4. [ROUTING.ja.md](ROUTING.ja.md) — ルート、3 段パイプライン、キュー駆動、SysEx の 3 規則、ループ防止。
5. [CORE_DESIGN.ja.md](CORE_DESIGN.ja.md) — core / ポート / example の境界、依存の規則、時間・並行性の境界(どのライブラリのコールバックがどのコンテキストで走るか)。
6. [PORTS.ja.md](PORTS.ja.md) — 全ポートの挙動・実装状況・依存・PC から見える構成。
7. [CONFIGURATION.ja.md](CONFIGURATION.ja.md) — 設定モデルのメモ。保存先や設定 UI を core の外に置く理由。
8. [DECISIONS.ja.md](DECISIONS.ja.md) — 設計決定の台帳。採らなかった案(UMP 内部表現、依存反転、MIDI 集約)も理由付きで残している。

**プロセス**

- [DEVELOPMENT_PLAN.ja.md](DEVELOPMENT_PLAN.ja.md) — 実装順、現在地、残作業。
- [LIBRARY_REQUESTS.ja.md](LIBRARY_REQUESTS.ja.md) — 基盤ライブラリへの変更依頼台帳。依頼不要と確認済みのものも記録している。
- [RELEASE_CHECKLIST.ja.md](RELEASE_CHECKLIST.ja.md) / [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) — リリース前の確認項目と、共通リリース workflow の動き。
- [../tests/README.ja.md](../tests/README.ja.md) — テスト構成(`unit/`、`arduino_smoke/`、`examples_compile/`、`loopback/`、`peer/`、`manual/`)と実行方法。
- [../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) — カバレッジ表。何をどこで確認していて、何が未着手か。

## 記述の約束

- 文書中のコード例では namespace(`espmidi::`)を省略しない(`using` に頼らない)。
- 設計文書(`DATA_MODEL` / `ROUTING` / `CORE_DESIGN`)は**仕様**を書く。実装状況は `DEVELOPMENT_PLAN.ja.md` と `PORTS.ja.md` の状況列に集約し、設計文書側で「実装済み / 未実装」を書き分けない。
- `PORTS.ja.md` は「予定」「実装済み(実機検証待ち)」「実装済み(実機検証済み)」を区別する。リリース可否の判断で状況列をそのまま信用できるようにするため。
- 設計を変えたら `DECISIONS.ja.md` に理由と、採らなかった案を残す。
- **使う人向けの文書(`GUIDE` / `MIDI_BASICS`)には仕様を書かない。** 正本は設計文書側に置き、こちらからは参照する。同じことを 2 箇所に書くと必ず片方が古くなる。
- **日英併記の文書は、日本語版を直したら英語版も直す。** 対応は `tests/unit/test_repository_structure.py` が固定している(`.ja.md` があって `.md` が無い、またはその逆を落とす)。**内部メモを英語にする必要はない。**
