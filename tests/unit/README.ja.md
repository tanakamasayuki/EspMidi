# Unit Tests

ハードウェアなしで実行できる core のテストを置きます。

subject ごとにディレクトリを分け、`conftest.py` の `cpp_test` fixture が `g++ -std=c++17 -funsigned-char -Wall -Wextra -Werror` で `src/` に対してコンパイルして実行します。arduino-cli を使わないので CI でも数秒で終わります。

```sh
uv run pytest unit/            # 全部
uv run pytest unit/version/    # 1 subject だけ
```

**Arduino ライブラリとしての include / build 経路はここでは確認しません。** それは [`../arduino_smoke/`](../arduino_smoke/) の担当です。

## この方式が成立する理由

core は Arduino / ESP-IDF / ハードウェアに依存しない純粋 C++ です([../../docs/CORE_DESIGN.ja.md](../../docs/CORE_DESIGN.ja.md))。`unit/version/version_test.cpp` は Arduino core が include パスに無い状態でコンパイルされるので、**core がうっかり Arduino に依存し始めた瞬間に落ちます**。その依存規則が他の全 unit test を軽く保っています。

## subject 一覧

実装順([../../docs/DEVELOPMENT_PLAN.ja.md](../../docs/DEVELOPMENT_PLAN.ja.md))に合わせて増やします。

| subject | 対象 | 状況 |
| --- | --- | --- |
| `version/` | 公開ヘッダがホスト単体でコンパイルでき、バージョンマクロが整合している | 実装済み |
| `port_model/` | Endpoint / Port / 席モデル / 状態 / メタデータ / ポート群 | 予定(Phase 2) |
| `routing/` | ルート / 3 段パイプライン / 登録順 / キュー / 循環検査 | 予定(Phase 3) |
| `sysex_rules/` | SysEx 3 規則(経路は開始時確定 / 切断時に閉じる / 出力排他) | 予定(Phase 3) |
| `filter/` | フィルタ条件 | 予定(Phase 4) |
| `transform/` | チャンネル / ノート / トランスポーズ / Velocity / CC 変換 | 予定(Phase 4) |

`test_repository_structure.py` はハードウェアも C++ も要らないリポジトリ構造の検査で、必須ファイルの存在と README の一覧一致を固定します。

## テストの書き方

```python
def test_something(cpp_test):
    output = cpp_test("something_test.cpp")
```

C++ 側は `assert` を並べ、最後に `TEST done <n>/<n>` を出力します。数が合わないと落ちるので、途中で早期 return したときに気付けます。
