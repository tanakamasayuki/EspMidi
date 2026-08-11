# リリースチェックリスト

[English](RELEASE_CHECKLIST.md)

`EspMidi` のリリース前に確認する項目です。バージョン更新とリリースは全プロジェクト共通の [arduino-library-release-toolkit](https://github.com/tanakamasayuki/arduino-library-release-toolkit) の運用に従い、`tools/bump_version.py` と `.github/workflows/release.yml` はこのリポジトリで編集しません(toolkit 側で直して配布する)。

## ドキュメント

- `README.ja.md` / `README.md` の機能表・ポート表・対応環境表が実装と合っている。
- `docs/PORTS.ja.md` の状況列(**予定** / **実装済み(実機検証待ち)** / **実装済み(実機検証済み)**)が実際の検証状況と合っている。
- `examples/README.ja.md` / `examples/README.md` の一覧が `examples/` の実体と一致している(`tests/unit/test_repository_structure.py` が自動確認)。
- `docs/REQUIREMENTS.ja.md` の要件と実装済み範囲の差分が説明できる。
- `docs/DATA_MODEL.ja.md` / `docs/ROUTING.ja.md` の仕様が実装と合っている。仕様を変えたなら `docs/DECISIONS.ja.md` に理由が残っている。
- `docs/CORE_DESIGN.ja.md` の core / ポート / example 境界とコールバック実行コンテキストの記述が実装と合っている。
- `docs/DEVELOPMENT_PLAN.ja.md` の現在地と残作業が最新になっている。
- `docs/LIBRARY_REQUESTS.ja.md` の依頼の状態が最新になっている(提案済み / 実装済み / 取り下げ)。
- `tests/README.ja.md` / `tests/TEST_PLAN.ja.md` が現状のテスト構成とカバレッジと合っている。

## メタデータ

- `library.properties` の `name`、`sentence`、`paragraph`、`architectures`、`includes` が公開内容と合っている。
- `keywords.txt` に公開 class、method、constant が入っている(新しいポート・API を足したら追加する)。
- 依存ライブラリの最低バージョン(EspUsbHost / EspUsbDevice / EspBle)が README・`docs/PORTS.ja.md`・`examples/**/sketch.yaml` の 3 箇所で一致している。
- `CHANGELOG.md` の `## Unreleased` に、今回入れる変更が漏れなく書かれている(bump 時にここが新しいバージョン節へ移る)。

## テスト

ハードウェア不要の 3 つは CI(`.github/workflows/tests.yml`)でも回るが、リリース前にローカルでも確認する。

```sh
cd tests
uv run pytest unit/ arduino_smoke/ examples_compile/
```

実機テストは接続できる範囲で実行し、結果を `tests/TEST_PLAN.ja.md` に反映する。

```sh
uv run --env-file .env pytest loopback/
uv run --env-file .env pytest peer/
```

`manual/` の手順は `tests/manual/README.ja.md` に残し、自動テストの合格条件と混ぜない。

## リリース作業

1. `## Unreleased` の内容と bump レベル(major / minor / patch)を確定する。

   ```sh
   python tools/bump_version.py --preview
   ```

2. GitHub Actions の `Release` workflow(workflow_dispatch)を実行する。共通 workflow が次を行う:
   - `library.properties` の version 更新、`CHANGELOG.md` の `## Unreleased` を新バージョン節へ移動、`src/espmidi_version.h` の再生成
   - default branch へのコミットと push
   - `release` branch の再作成(`examples/**/sketch.yaml` の `dir: ../../` を `EspMidi (<version>)` へ書き換えてコミット、`tests/` を削除)
   - tag / ZIP / GitHub release の作成

3. 最終 diff に build artifact、cache、ローカル profile 固有の変更が入っていないことを確認する。

## リリース後

- Arduino Library Manager に新しいバージョンが載ったことを確認する(反映まで数時間かかる)。
- `examples/**/sketch.yaml` は default branch では `dir:` 参照のままであることを確認する(書き換えは `release` branch のみ)。
