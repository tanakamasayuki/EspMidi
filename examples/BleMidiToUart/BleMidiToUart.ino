// A wireless BLE MIDI keyboard playing a MIDI DIN sound module.
//
// This is UC5 in docs/USE_CASES.ja.md: the board makes an old sound module
// playable from a modern wireless keyboard, with no PC involved. It also works the
// other way — anything arriving on the MIDI DIN input is sent to the keyboard, so a
// controller with lights or a display can be driven back.
//
//   BLE MIDI keyboard ──→ MIDI DIN OUT (sound module)
//   MIDI DIN IN ────────→ BLE MIDI keyboard
//
// The sketch scans and connects, because that is BLE and EspMidi does not own it.
// Finding the MIDI service on the connection is the port's job, so discover() is
// never called here.
//
// **Nothing names the keyboard.** Its seat appears when the connection becomes
// usable, so the routes are written from InGroup::all() and whatever arrives takes
// part. The keyboard also gets its seat back when it reconnects: a BLE address is
// the identity, so the routes survive it going out of range.
//
// Wiring: RX_PIN to the MIDI IN, TX_PIN to the MIDI OUT. A real MIDI DIN socket
// needs an optocoupler on the input and a 220Ω pair on the output.

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
