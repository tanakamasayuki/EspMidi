# EspMidi 要件定義

## 1. 概要

EspMidiは、Arduino環境のESP32シリーズを対象とした、複数のMIDIインターフェースを統合管理するためのライブラリである。

USB Device、USB Host、Bluetooth Low Energy、UARTなど、異なる方式で接続されたMIDI入出力を共通の考え方で扱い、インターフェース間の変換、複製、統合、振り分けを行えることを目的とする。

EspMidi自身がUSBやBluetoothの通信基盤全体を所有するのではなく、既存の汎用ライブラリが提供するMIDI機能を組み合わせ、その上にMIDI共通処理を提供する。

対象とする主な基盤ライブラリは以下である。

* EspUsbHost
* EspUsbDevice
* EspBle
* EspBleBluedroid

EspUsbHostはUSB Host上でMIDIを含む複数のUSBクラスを同時に扱える汎用ライブラリであり、複数デバイスや複合デバイスを対象としている。

EspUsbDeviceはUSB MIDIに加えてHID、CDC、MSC、Audioなどを組み合わせた複合USB Deviceを構成できる。

EspBleはBLEのCentral、Peripheral、GATT、HID、MIDIなどを共通のBLE基盤上で扱う汎用ライブラリであり、BLE MIDIのDevice側とHost側の双方を対象としている。

EspMidiは、これらの汎用ライブラリをMIDI専用ライブラリの内部へ隠すのではなく、アプリケーションから引き続き直接利用できる状態を維持する。

---

## 2. 目的

EspMidiの目的は、ESP32上の異なるMIDI入出力を、接続方式を意識しすぎずに組み合わせられる統合層を提供することである。

主な目的は以下とする。

* 異なるMIDIインターフェースを共通の概念で扱う
* 単独のMIDIポートを簡単に利用できる
* 複数の入力または出力を一つのまとまりとして扱える
* MIDIインターフェース間でメッセージを転送できる
* 一つの入力を複数の出力へ複製できる
* 複数の入力を一つの出力へ統合できる
* MIDIメッセージを条件に応じて振り分けられる
* MIDIメッセージのフィルタリングや基本的な変換を行える
* USBやBLEのMIDI以外の機能と共存できる
* 各インターフェースの初期化やライフサイクルをアプリケーション側で管理できる
* ESP32シリーズ間のハードウェア差やBLEバックエンド差を、既存の各汎用ライブラリ側で吸収できる

---

## 3. 対象環境

### 3.1 対象プラットフォーム

対象は以下に限定する。

* Arduinoフレームワーク
* Arduino-ESP32
* ESP32シリーズ

汎用C++ライブラリや、ESP32以外のArduinoボードへの移植性は主要目的としない。

ただし、MIDIメッセージ処理やルーティングなど、ハードウェアに依存しない部分については、可能な範囲でESP32固有APIへの依存を避ける。

### 3.2 対象ESP32

実際に利用できるインターフェースは、各ESP32 SoCの機能と基盤ライブラリの対応範囲によって異なる。

例えば、USB HostおよびUSB DeviceはUSB OTG機能を持つESP32-S2、ESP32-S3、ESP32-P4などが主な対象となる。EspUsbHostはESP32-S2、ESP32-S3、ESP32-P4を対象としている。

BLEについては、SoCやArduino-ESP32のBluetoothバックエンドに応じてEspBleまたはEspBleBluedroidを利用する。

EspMidiは、すべてのESP32で全インターフェースが利用できることを保証するものではない。

---

## 4. 基本コンセプト

### 4.1 MIDI処理と通信基盤を分離する

EspMidiはUSB Host、USB Device、BLEなどの通信基盤を初期化、停止、破棄しない。

通信基盤の管理は、それぞれの汎用ライブラリとアプリケーションが担当する。

EspMidiは、それらが提供するMIDI入出力を登録し、共通化し、組み合わせる。

概念的な責務の分担は次のとおりである。

```text
アプリケーション
├─ EspUsbHost
│  ├─ MIDI
│  ├─ HID
│  ├─ CDC
│  ├─ MSC
│  └─ その他のUSBクラス
├─ EspUsbDevice
│  ├─ MIDI
│  ├─ HID
│  ├─ CDC
│  └─ その他のUSBクラス
├─ EspBle / EspBleBluedroid
│  ├─ MIDI
│  ├─ HID
│  ├─ 独自GATTサービス
│  └─ その他のBLE機能
└─ EspMidi
   ├─ MIDIポートの統合
   ├─ MIDIルーティング
   ├─ MIDIフィルタ
   ├─ MIDI変換
   └─ 複数ポート管理
```

### 4.2 外部所有を基本とする

EspMidiへ登録されるUSB、BLEなどのオブジェクトは、原則として外部所有とする。

EspMidiは以下を行わない。

* USB Hostスタック全体の起動
* USB Deviceスタック全体の起動
* BLEスタック全体の起動
* BLE Advertising全体の管理
* USBやBLEの停止
* 外部オブジェクトの破棄
* MIDI以外のUSBクラスやBLEサービスの管理

これにより、MIDIとHID、CDC、Audio、独自GATTサービスなどを同じアプリケーション内で共存させられることを重視する。

### 4.3 接続方式ではなくMIDIポートを中心に扱う

EspMidiの中心概念は、USB、BLE、UARTといった通信方式そのものではなく、MIDIメッセージを送受信する論理的なポートである。

各インターフェースは、一つ以上のMIDI入力またはMIDI出力を提供する。

```text
USB Host MIDI
USB Device MIDI
BLE MIDI
UART MIDI
        ↓
共通のMIDIポート
        ↓
ルーティング、統合、変換
```

アプリケーションは、可能な限り同じ考え方で各ポートを扱えることを目指す。

---

## 5. 対象MIDIインターフェース

### 5.1 初期対象

初期の標準対象は以下とする。

* USB Device MIDI
* USB Host MIDI
* BLE MIDI Device
* BLE MIDI Host
* UART MIDI

USBおよびBLEについては、既存の汎用ライブラリが提供するMIDI APIを利用する。

UART MIDIはEspMidiリポジトリ内で標準機能として提供してよい。ただし、UARTはMIDI統合処理とは分離されたインターフェース機能として扱う。

### 5.2 USB Device MIDI

USB Device MIDIはEspUsbDeviceを利用する。

USB MIDIだけを単独で公開する構成と、HID、CDC、MSCなどを含む複合USB Deviceの双方で利用できることを要件とする。

EspUsbDeviceはUSB MIDIイベントパケットの送受信に対応し、MIDIと他のUSB機能を組み合わせた複合デバイスを構成できる。

EspMidiはUSB Device全体を初期化せず、EspUsbDevice上のMIDI機能のみを統合対象とする。

### 5.3 USB Host MIDI

USB Host MIDIはEspUsbHostを利用する。

以下を考慮する。

* USB機器の動的な接続と切断
* 複数USB MIDI機器
* USBハブ経由の機器
* MIDI以外のUSB機器との共存
* MIDIを含む複合USB機器
* 一つのUSB接続が複数の論理MIDIポートを持つ場合

EspUsbHostはUSB MIDIだけでなく、HID、CDC、Audio、MSC、Networkなど複数のUSBクラスを同一基盤で扱い、複数デバイスを識別して利用できる。

EspMidiはUSB Host全体を専有せず、USB Host上で発見されたMIDI機能のみを統合する。

### 5.4 BLE MIDI

BLE MIDIはEspBleまたはEspBleBluedroidを利用する。

以下の両方を対象とする。

* ESP32がBLE MIDI Deviceとして動作する
* ESP32がBLE MIDI Hostとして外部機器へ接続する

BLE MIDIは、他のGATTサービスやBLE HIDと共存できることを要件とする。

EspBleではBLE MIDIとHID、独自GATTサービス、Central、Peripheralなどが同じ基盤を共有する設計となっている。

EspMidiはBLEスタック、接続、セキュリティ、Advertisingなどの一般的なBLE管理を担当しない。

### 5.5 UART MIDI

UART MIDIは標準的なMIDI入出力としてEspMidiリポジトリへ同梱してよい。

UART MIDIはUSBやBLEとは異なり、MIDIプロトコルと物理UARTとの結び付きが強いため、標準機能として提供する合理性がある。

ただし、UART MIDIも統合コアとは分離して扱う。

EspMidi全体の管理対象に登録された後は、USBやBLEのMIDIポートと同じ考え方で利用できることを目指す。

---

## 6. 複数ポートの統合

### 6.1 単独利用

各MIDIポートは、他のポートを登録しなくても単独で使用できること。

例えば以下の用途を許容する。

* UART MIDIのみ
* USB Device MIDIのみ
* USB Host MIDIのみ
* BLE MIDIのみ

EspMidiを使用するために複数のインターフェースを必須としてはならない。

### 6.2 一対一の変換

一つのMIDI入力を一つのMIDI出力へ転送できること。

代表例は以下である。

* USB HostからUART
* UARTからUSB Device
* BLE HostからUSB Device
* USB HostからBLE Device

### 6.3 一対多の複製

一つの入力を複数の出力へ送信できること。

```text
USB Host入力
├─ UART出力
├─ USB Device出力
└─ BLE Device出力
```

### 6.4 多対一の統合

複数の入力を一つの出力へ統合できること。

```text
UART入力 ──────┐
USB Host入力 ──┼─ USB Device出力
BLE Host入力 ──┘
```

### 6.5 多対多の構成

複数の入力と複数の出力を任意に組み合わせられること。

固定された「全入力から全出力へ転送」という構成だけでなく、入力元と出力先の組み合わせを個別に管理できることを目指す。

### 6.6 ポートのグループ化

複数のMIDIポートを、一つの論理的な入力群または出力群として扱えること。

ポート群は単なる全体一括転送だけでなく、用途に応じて複数定義できることが望ましい。

例：

* すべての出力
* 外部シンセ出力
* 制御用ポート
* クロック送信用ポート
* モニター用ポート

---

## 7. MIDIルーティング

EspMidiは、入力元と出力先の対応関係を管理できること。

ルーティングは通信方式ではなく、論理的なMIDIポート間の関係として扱う。

ルーティングには以下の性質を求める。

* 入力元を識別できる
* 出力先を指定できる
* 一つの入力に複数の出力を割り当てられる
* 複数の入力を同じ出力へ割り当てられる
* 接続中に構成を変更できる
* USB HostやBLE Hostの接続・切断に追従できる
* 無効になったポートを安全に扱える
* 意図しない循環転送を防止できる

ルーティング処理は、各USBまたはBLEライブラリの内部へ分散させず、EspMidi側で統一的に扱う。

---

## 8. MIDIメッセージ処理

### 8.1 共通表現

異なるインターフェースから受信したMIDIメッセージを、EspMidi内では共通の意味として扱えること。

通信方式固有のパケット形式は各インターフェース側で処理し、統合処理ではNote、Control Change、Program ChangeなどのMIDI上の意味を中心に扱う。

### 8.2 送信元情報

受信したMIDIメッセージについて、どのポートから受信したかを識別できること。

複数入力を統合した後も、必要に応じて送信元を参照できること。

### 8.3 MIDI 1.0

初期の主要対象はMIDI 1.0とする。

少なくとも以下を対象とする。

* Note On
* Note Off
* Polyphonic Key Pressure
* Control Change
* Program Change
* Channel Pressure
* Pitch Bend
* System Common
* System Real-Time
* System Exclusive

### 8.4 System Exclusive

System Exclusiveは、短いメッセージだけを前提としない。

USB、BLE、UART間でメッセージサイズや分割方法が異なることを考慮し、長いSysExを扱えることを要件とする。

### 8.5 タイミング情報

BLE MIDIなど、通信方式側がタイミング情報を持つ場合、その情報を完全に失わない設計を目指す。

ただし、異なる通信方式間での厳密な時刻同期や高精度スケジューリングは、初期必須要件とはしない。

---

## 9. フィルタリングと変換

EspMidiは、MIDIインターフェース間の単純転送だけでなく、基本的な条件指定と変換を行えることを目指す。

### 9.1 フィルタリング

以下を条件として、メッセージを通過または破棄できること。

* MIDIメッセージ種別
* MIDIチャンネル
* ノート範囲
* Control Change番号
* 送信元ポート
* その他の基本的なMIDI属性

### 9.2 基本変換

以下のような一般的な変換を対象とする。

* MIDIチャンネルの変更
* ノート番号の変更
* トランスポーズ
* Velocity値の変更
* Control Change番号の変更
* Control Change値の範囲変換

高度なシーケンサー、MIDIエフェクター、スクリプトエンジンを内蔵することは目的としない。

---

## 10. GPIOおよびコントロールヘルパー

### 10.1 位置付け

GPIO、ADC、エンコーダなどはMIDI通信インターフェースそのものではない。

これらは外部入力をMIDIメッセージへ変換したり、MIDIメッセージを外部出力へ反映したりするコントロール機能として扱う。

### 10.2 同梱方針

GPIO系のヘルパーはEspMidiリポジトリへ同梱してよい。

ただし、MIDI統合コアの必須部分ではなく、任意に利用する補助機能として明確に分離する。

概念上は「GPIO機能」よりも「Control Mapping機能」として整理する。

対象例：

* ボタンからNote
* ボタンからControl Change
* アナログ入力からControl Change
* エンコーダからControl Change
* Note受信によるLED制御
* Control Change受信による出力制御
* 外部クロックとMIDI Clockの変換

### 10.3 外部I/Oライブラリとの共存

コントロールヘルパーは、ESP32のGPIO番号だけに固定しないことが望ましい。

以下のような入力元を同じ考え方で利用できる余地を残す。

* ESP32 GPIO
* ADC
* タッチ入力
* I/Oエキスパンダ
* キーマトリクス
* 外部入力ライブラリ

GPIO初期化、デバウンス、I/Oエキスパンダ管理などを、EspMidiが必ず所有する設計にはしない。

---

## 11. ESP32KeyBridgeとの関係

EspMidiは、ESP32KeyBridgeと近い統合思想を持つ。

ESP32KeyBridgeは、複数の入力アダプタを共通状態へ正規化し、変換を適用し、必要に応じて複数入力を統合し、一つ以上の出力アダプタへ送るライブラリとして設計されている。

EspMidiでも、同様に次の分離を採用する。

```text
入力インターフェース
        ↓
共通MIDI表現
        ↓
統合・変換・振り分け
        ↓
出力インターフェース
```

共通する設計方針は以下である。

* 複数インターフェースを統合する
* 入出力方式と変換処理を分離する
* 一つ以上の出力へ送信できる
* 共通の中間表現を持つ
* ハードウェア固有処理をアダプタ側へ分離する
* GPIOやUARTなどの標準的な補助機能を統合リポジトリへ同梱できる
* USBとBLEは既存の汎用ライブラリを利用する
* 各汎用ライブラリへ必要な変更を加えられることを前提とする

一方、キーボードとMIDIでは状態モデルが異なるため、ESP32KeyBridgeの内部モデルをそのまま再利用することは目的としない。

設計思想、リポジトリ構成、ドキュメント構成、テスト方針などを共有することは望ましい。

---

## 12. リポジトリの責務

EspMidiリポジトリには、概念上以下の領域を含める。

### 12.1 必須領域

* MIDIメッセージの共通表現
* MIDI入出力ポートの共通概念
* 複数ポートの登録と管理
* ポートのグループ化
* MIDIルーティング
* MIDIフィルタリング
* 基本的なMIDI変換
* 接続・切断への追従
* ループや意図しない自己転送の防止

### 12.2 標準インターフェース領域

* UART MIDI
* EspUsbHostとの連携
* EspUsbDeviceとの連携
* EspBleとの連携
* EspBleBluedroidとの連携

USBおよびBLE連携の具体的な実装場所は、EspMidi側と各汎用ライブラリ側のどちらでもよい。

ただし、利用者から見たMIDI APIと挙動は可能な限り統一する。

### 12.3 任意ヘルパー領域

* ボタン、アナログ入力、エンコーダなどからのMIDI生成
* MIDI受信によるLEDや外部出力の制御
* MIDI Clockと外部パルスの変換
* 開発用のモニターや診断機能

これらは統合コアと分離し、不要な場合に使用しなくてもよい構成とする。

---

## 13. 非目標

初期段階では、以下を主要目的としない。

### 13.1 AppleMIDIおよびRTP-MIDI

AppleMIDIやRTP-MIDIは、ネットワーク、セッション、同期、探索、再接続など、MIDIポート統合とは別の責務が大きい。

そのため、初期の標準対象には含めない。

将来、外部拡張としてMIDIポートへ接続できる設計は維持してよい。

### 13.2 あらゆるMIDI通信方式への対応

以下を初期標準機能として要求しない。

* RTP-MIDI
* Web MIDI
* WebSocket MIDI
* OSC
* ESP-NOW MIDI
* 独自無線MIDI

### 13.3 DAWやシーケンサー機能

EspMidiは以下を目的としない。

* MIDIシーケンサー
* 楽曲編集
* ピアノロール
* SMFファイルの総合編集
* DAW機能
* ソフトウェアシンセサイザー
* 音声生成

### 13.4 USBおよびBLE基盤の再実装

EspUsbHost、EspUsbDevice、EspBle、EspBleBluedroidが担う汎用機能をEspMidi内へ重複実装しない。

### 13.5 MIDI以外の通信データの統合

EspMidiはUSB HID、USB Audio、BLE HID、CDC、MSCなどのデータを統合管理しない。

ただし、それらが同じUSBまたはBLE基盤上でMIDIと共存することは重要な要件とする。

---

## 14. 共存性

EspMidiの重要な価値は、MIDI以外の機能と共存できることである。

以下のような構成を妨げないこと。

* USB DeviceでMIDIとHIDを同時に公開する
* USB DeviceでMIDIとCDCを同時に公開する
* USB HostでMIDI機器とキーボードを同時に利用する
* USB HostでMIDIを含む複合機器を利用する
* BLE MIDIとBLE HIDを同時に利用する
* BLE MIDIと独自GATTサービスを同時に利用する
* USB Host、USB Device、BLE、UARTを同時に利用する
* EspMidi以外の処理が各汎用ライブラリへ直接アクセスする

EspMidiを使用したことによって、USBやBLEの基盤オブジェクトが隠蔽されてはならない。

---

## 15. ライフサイクル方針

USBおよびBLE基盤の初期化順序はアプリケーションが決定する。

EspMidiは、すでに利用可能なMIDIインターフェースを登録できることを基本とする。

動的な接続を持つインターフェースについては、次を扱えること。

* MIDIポートの追加
* MIDIポートの削除
* 一時的な切断
* 再接続
* 接続先の変更
* 複数機器の識別

EspMidiの開始と終了が、外部所有されているUSBまたはBLE基盤の開始と終了を暗黙に引き起こしてはならない。

---

## 16. エラーと状態の可視性

統合によって各インターフェースの状態が隠れすぎないこと。

少なくとも概念上、以下を確認できることが望ましい。

* ポートが利用可能か
* ポートが接続中か
* メッセージを送信できるか
* 送信が失敗したか
* 入力が破棄されたか
* 出力が混雑しているか
* どのポートで問題が発生したか
* USBまたはBLE機器が切断されたか

EspMidiは、異なるインターフェースの差を共通化しつつ、診断に必要な情報まで消してはならない。

---

## 17. 拡張性

EspMidiは、将来新しいMIDIインターフェースを追加できること。

追加対象は必ずしもEspMidiリポジトリ内に存在する必要はない。

外部ライブラリがEspMidiの共通ポートとして参加できる設計を目指す。

将来候補には以下がある。

* MIDI 2.0
* Universal MIDI Packet
* RTP-MIDI
* 独自ネットワークMIDI
* 外部UART拡張
* SPIやI2C経由のMIDIブリッジ
* CV/GateやDIN Syncとの変換

ただし、将来の可能性のために初期設計を過度に複雑化しない。

---

## 18. 開発方針

EspMidiと連携する主要ライブラリは同一作者の管理下にあるため、統合に必要な変更はEspMidi側の回避処理だけで済ませず、適切な責務を持つ基盤ライブラリ側へ反映できる。

開発時は以下を重視する。

* ライブラリ間でMIDI用語と意味を統一する
* 同じMIDI機能に異なる挙動を持たせない
* USB Host、USB Device、BLE間で可能な範囲でAPIを揃える
* 共通化のために各基盤の重要情報を失わない
* 一つのライブラリに責務を集中させすぎない
* 実機によるインターフェース間テストを行う
* 単独インターフェースでも利用可能にする
* 複合構成を主要なテスト対象にする

---

## 19. 初期マイルストーン

初期リリースでは、以下を優先する。

1. 共通MIDIメッセージ表現
2. MIDI入力および出力ポートの統一
3. EspUsbHost MIDIとの連携
4. EspUsbDevice MIDIとの連携
5. EspBleおよびEspBleBluedroid MIDIとの連携
6. UART MIDI
7. 一対一のインターフェース変換
8. 一対多の出力
9. 多対一の入力統合
10. 基本的なフィルタリング
11. 基本的なチャンネルおよびノート変換
12. USBとBLEの動的接続・切断への追従
13. MIDI以外のUSBクラスおよびBLEサービスとの共存確認
14. GPIO、アナログ入力、エンコーダ向けの最小限のControl Mappingヘルパー

AppleMIDI、MIDI 2.0、高度な変換、永続設定、設定UIなどは、初期リリースの必須要件から外す。

---

## 20. 要約

EspMidiは、MIDI通信方式そのものを再実装するライブラリではない。

EspUsbHost、EspUsbDevice、EspBle、EspBleBluedroidなどが提供するMIDI機能とUART MIDIを、共通のポートとして統合するライブラリである。

中心となる責務は以下である。

```text
MIDIポートの共通化
        ↓
複数ポートの登録
        ↓
統合、複製、振り分け
        ↓
フィルタリングと基本変換
```

USBやBLEの初期化と所有権は外部に残し、MIDI以外のUSBクラスやBLEサービスとの共存を優先する。

UART MIDIとControl MappingヘルパーはEspMidiリポジトリへ同梱してよいが、統合コアとは明確に分離する。

AppleMIDIなどのネットワークMIDIは初期スコープへ含めず、将来外部拡張として接続可能な余地を残す。

ESP32KeyBridgeと同様に、入力、共通表現、変換、出力を分離し、ESP32上の複数インターフェースを一つのシステムとして扱えることをEspMidiの基本方針とする。
