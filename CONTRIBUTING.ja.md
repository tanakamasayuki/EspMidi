# 開発の進め方

[English](CONTRIBUTING.md)

このリポジトリで手を入れるときの手順と約束です。設計の背景は [docs/README.ja.md](docs/README.ja.md) から辿れます。

## テストを回す

```sh
cd tests
uv run pytest unit/            # ハードウェア不要。数秒
```

`unit/` は `g++` だけで回ります。**arduino-cli も board package も要りません。** `unit/version` は Arduino core が include パスに無い状態でコンパイルされるので、**core がうっかり Arduino に依存し始めた瞬間に落ちます。**

```sh
uv run pytest arduino_smoke/      # Arduino ライブラリとして解決できるか
uv run pytest examples_compile/   # examples が全部ビルドできるか
```

実機が要るものは `.env` を作ってから回します(`.env.example` を写して port を書き換える)。

```sh
uv run --env-file .env pytest loopback/   # 1 台。UART は配線ゼロ
uv run --env-file .env pytest peer/       # 2 台
```

**`peer/` は常時接続のボードに書き込みます。** 兄弟プロジェクトと共用なので、走らせるとそちらのファームウェアは消えます。

## どこにテストを置くか

| 確認したいこと | 置く場所 |
| --- | --- |
| 仕様(メッセージ、ルーティング、フィルタ、**ポートの挙動**) | `unit/` |
| Arduino ライブラリとしての解決 | `arduino_smoke/` |
| examples がビルドできる | `examples_compile/` |
| 1 台で複数ポートを往復 | `loopback/` |
| 2 台でポートの境界 | `peer/` |
| 人の目や操作が要る | `manual/`(手順だけ。**自動テストの合格条件に混ぜない**) |

**できるだけ `unit/` に寄せてください。** ポートもテンプレートにして偽物を当てれば `unit/` で固定できます([docs/PORT_AUTHORING.ja.md](docs/PORT_AUTHORING.ja.md))。実機に残すのは**実機でしか確認できないことだけ**です。

## 実機テストのスケッチの書き方

**`setup()` で結果を出さないでください。** 書き込みツールがボードをリセットし、コンソールが開かれるのはその後なので、起動直後の出力は誰も聞いていないうちに終わります。空のログだけが残ります。

- 準備完了は**繰り返し**告げる
- 本番はホストに促されてから走らせる。**促しは「決めた 1 文字」で判定する** — 書き込みツールが線を離すときに紛れ込む 1 バイトで走り出すと、やはり全部終わってしまいます(ESP32-P4 で実際に起きました)

## コードの約束

- **core は Arduino / ESP-IDF / ハードウェアに依存しない。** `src/EspMidi.h` から include されるものは純粋 C++ で保ちます
- **ヒープを使わない。** 記憶域は固定長で、上限は `ESPMIDI_*` で変えられる形にします
- **`std::function` を毎メッセージの経路に置かない。** 関数ポインタ + `void *context` を使います(通知は機器の列挙中にトランスポートのタスクから走ります)
- **例外も RTTI も使わない**
- コンパイルは `-std=c++17 -funsigned-char -Wall -Wextra -Werror`。**ヘッダの警告は欠陥**です
- 名前空間は省略しない。文書のコード例でも `espmidi::` を書きます

### コメント

**「何を」ではなく「なぜ」を書きます。** 特に、そうしなかった選択肢が分かるように書きます。

```cpp
// Note off rather than a note on with velocity 0: both stop the note, and
// the explicit one is what a receiver with release velocity expects.
```

型や関数の宣言を日本語に訳しただけのコメントは書きません。

## 文書の約束

日英併記の範囲は 3 段です([docs/README.ja.md](docs/README.ja.md))。

| 区分 | 言語 |
| --- | --- |
| 使う人が読むもの(README、GUIDE、MIDI_BASICS、RECIPES、API、FOOTPRINT、PORT_AUTHORING、example) | **日英** |
| 確定した仕様(DATA_MODEL、ROUTING、PORTS) | **日英** |
| 内部の記録・作業メモ(REQUIREMENTS、USE_CASES、CORE_DESIGN、CONFIGURATION、DECISIONS、DEVELOPMENT_PLAN、LIBRARY_REQUESTS、テスト計画) | 日本語のみ |

**正本は日本語版**です。`tests/unit/test_repository_structure.py` が対応を固定しているので、片方だけ足すと落ちます。

- **設計文書には仕様を書き、実装状況は書かない。** 状況は `DEVELOPMENT_PLAN.ja.md` と `PORTS.ja.md` の状況列に集約します
- **使う人向けの文書には仕様を書かない。** 正本を参照します。同じことを 2 箇所に書くと必ず片方が古くなります
- **文書のコード例は `tests/unit/docs_snippets` でコンパイルされます。** API 名を変えたらここが落ちるので、文書が黙って古くなりません
- 設計を変えたら `DECISIONS.ja.md` に理由と、採らなかった案を残します

## examples の約束

**すべて実用例**です。そのまま書き込んで使えるスケッチにします。

```text
examples/<Name>/
  <Name>.ino      スケッチ名とディレクトリ名を一致させる
  README.ja.md
  README.md
  sketch.yaml     default_profile を設定する
```

- 構成は「1) スタック起動 → 2) ポート生成 → 3) ルート」の 3 段に揃える
- README の一覧と実体の一致は構造テストが固定します
- 兄弟ライブラリは**公開バージョンを指定**し、開発版を試すための `*_local` プロファイルを並べます

## リリース

[docs/RELEASE_CHECKLIST.ja.md](docs/RELEASE_CHECKLIST.ja.md) の手順です。`tools/bump_version.py` と `.github/workflows/release.yml` は**共通ツールキットから写したもので、このリポジトリでは編集しません**。

`manual/` の手順は**リリース前に人が一度通す**必要があります。自動化しないと決めた分です。
