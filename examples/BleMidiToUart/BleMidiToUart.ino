// en: Route BLE MIDI and UART MIDI bidirectionally
// ja: BLE MIDIとUART MIDIを双方向にルートする
#include <EspMidiEspBle.h>
#include <EspMidiUart.h>

static const int8_t RX_PIN = 20;
static const int8_t TX_PIN = 19;

EspBle ble;
EspBleMidiHost bleMidi(ble);

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::BleHostPort keyboard(router, bleMidi, ble);
espmidi::UartPort din(router, Serial1, 1);

bool connectRequested = false;

void onPortEvent(void *, const espmidi::PortEvent &event)
{
  espmidi::PortInfo info;
  if (!registry.portInfo(event.port, info))
  {
    return;
  }
  Serial.print(info.name);
  Serial.print(info.direction == espmidi::Direction::In ? " (in) " : " (out) ");
  Serial.println(info.state == espmidi::PortState::Available ? "available" : "disconnected");
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  // 1) The stacks, and the scanning that finds a keyboard.
  // en: Initialize this component after its prerequisites; inspect the returned status where provided.
  // ja: 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
  ble.begin();
  bleMidi.begin();
  ble.scanner().onResult([](const EspBleScanResult &result) {
    if (connectRequested || !result.advertisesService(ESP_BLE_MIDI_SERVICE_UUID))
    {
      return;
    }
    ble.scanner().stop();
    connectRequested = ble.connect(result);
  });
  ble.onDisconnected([](const EspBleConnection &) {
    // Scan again, so the keyboard is picked up when it comes back. Its seat is
    // still there waiting for it.
    connectRequested = false;
    EspBleScanConfig scan;
    scan.active = true;
    // en: Start work using the configured entry point; the context and objects must remain valid while it runs.
    // ja: 設定した入口から処理を開始します。実行中はコンテキストとオブジェクトの寿命を保ちます。
    ble.scanner().start(scan);
  });

  // 2) The ports. The BLE port supplies nothing yet — it has not met a keyboard.
  keyboard.begin();
  din.begin("MIDI DIN", RX_PIN, TX_PIN);

  // 3) The routes. Both are written against groups rather than against a device,
  //    so neither has to be rebuilt when the keyboard comes and goes:
  //
  //      whatever plays  -> the sound module
  //      the MIDI DIN in -> whatever is connected
  //
  //    A route never sends a message back to the endpoint it came from, so the
  //    second one reaches the keyboard and not the module's own output.
  // en: Register source and destination endpoints/groups, considering disconnects and unintended loops.
  // ja: 切断と意図しないループを考慮して、入力と出力のEndpoint／グループを登録します。
  router.addRoute(espmidi::InGroup::all(), din.out());
  router.addRoute(din.in(), espmidi::OutGroup::all());
  registry.addListener(onPortEvent);

  EspBleScanConfig scan;
  scan.active = true;
  ble.scanner().start(scan);
  Serial.println("EspMidi BLE bridge ready — turn on a BLE MIDI keyboard");
}

void loop()
{
  ble.update();
  keyboard.update();
  din.update();
  router.update();
}
