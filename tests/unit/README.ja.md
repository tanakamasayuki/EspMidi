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

## ポートもここに入る

`uart_port/` だけは core ではなくポートのテストです。ポートがシリアルオブジェクトのテンプレート(`espmidi::BasicUartPort<T>`)になっているので、`HardwareSerial` の代わりに偽物を当てれば**ポートの挙動そのものをホスト上で固定できます**。

そこまでやると、実機のテストに残るのは**バイトが本当にパッドを渡ること**だけになります。逆に言えば、実機でしか確認できないことだけを [`../loopback/`](../loopback/) と [`../peer/`](../peer/) に残す、というのがポートを追加するときの方針です。

## subject 一覧

実装順([../../docs/DEVELOPMENT_PLAN.ja.md](../../docs/DEVELOPMENT_PLAN.ja.md))に合わせて増やします。

| subject | 対象 | 状況 |
| --- | --- | --- |
| `version/` | 公開ヘッダがホスト単体でコンパイルでき、バージョンマクロが整合している | 実装済み |
| `message/` | 共通表現。ステータス分類 / データ長表 / UMP 番号体系 / タイムスタンプの単位 / 短いメッセージの構築と直列化 | 実装済み |
| `parser/` | MIDI 1.0 バイトストリーム。running status / real-time の割り込み / SysEx のチャンク化と打ち切り | 実装済み |
| `serializer/` | `Message` → MIDI 1.0 バイト列。SysEx の枠付け / 送信中の中断 / パーサとの往復 | 実装済み |
| `usb_packet/` | USB MIDI イベントパケット。CIN 表 / cable → ポート / SysEx の組み立てと分解 / 往復 | 実装済み |
| `port_model/` | Endpoint / Port / 席モデル / 再接続の照合 / 状態伝播 / ポート群 / 通知 | 実装済み |
| `routing/` | ルート / 3 段パイプライン / 登録順 / キュー駆動 / ループ規則 / アプリケーションポート / 診断カウンタ | 実装済み |
| `sysex_rules/` | SysEx 3 規則(経路は開始時確定 / 切断時に閉じる / 出力排他)とチャンクの分割 | 実装済み |
| `filter/` | メッセージ種別の判定 / 種別・チャンネル・ノート範囲・CC 番号のフィルタ / 段への適用 | 実装済み |
| `transform/` | ValueMap の正規化 / チャンネル / トランスポーズ / Velocity / CC / プレッシャー / 段の合成 | 実装済み |
| `uart_port/` | UART ポート。席の供給 / 受信の router 到達 / 読み取りの上限 / 送信バッファ満杯 / 中断したダンプの終端 | 実装済み |

`test_repository_structure.py` はハードウェアも C++ も要らないリポジトリ構造の検査で、必須ファイルの存在と README の一覧一致を固定します。

## テストの書き方

```python
def test_something(cpp_test):
    output = cpp_test("something_test.cpp")
```

C++ 側は `assert` を並べ、最後に `TEST done <n>/<n>` を出力します。数が合わないと落ちるので、途中で早期 return したときに気付けます。
