// en: Use common routing logic with different transports
// ja: Transportが異なっても共通のルート処理を使う
// --- change this one line -------------------------------------------------
#define MIDI_PORT MIDI_PORT_UART
// -------------------------------------------------------------------------

#define MIDI_PORT_UART 1
#define MIDI_PORT_USB 2
#define MIDI_PORT_BLE 3

// --- the part that depends on the interface -------------------------------

#if MIDI_PORT == MIDI_PORT_UART

#include <EspMidiUart.h>
espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::UartPort port(router, Serial1, 1);

void startStack() {}                     // a UART port opens its own serial port
void updateStack() {}
// en: Initialize this component after its prerequisites; inspect the returned status where provided.
// ja: 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
void startPort() { port.begin("MIDI DIN", 20, 19); }

#elif MIDI_PORT == MIDI_PORT_USB

#include <EspMidiEspUsbDevice.h>
EspUsbDevice usb;
EspUsbDeviceMidi usbMidi(usb, 1, 1);
espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::UsbDevicePort port(router, usbMidi, usb);

void startStack()
{
  EspUsbDeviceConfig config;
  config.manufacturer = "EspMidi";
  config.product = "EspMidi Echo";
  usb.begin(config);
}
void updateStack() { usb.task(); }
void startPort() { port.begin("USB MIDI"); }

#else

#include <EspMidiEspBle.h>
EspBle ble;
EspBleMidiDevice bleMidi(ble);
espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::BleDevicePort port(router, bleMidi);

void startStack()
{
  bleMidi.begin(); // the GATT service is registered before the server starts
  ble.begin();
  ble.advertising().setName("EspMidi Echo");
  // en: Start work using the configured entry point; the context and objects must remain valid while it runs.
  // ja: 設定した入口から処理を開始します。実行中はコンテキストとオブジェクトの寿命を保ちます。
  ble.advertising().start();
}
void updateStack() { ble.update(); }
void startPort() { port.begin("BLE MIDI"); }

#endif

// --- the MIDI part: identical for all three -------------------------------

espmidi::AppPort sketch(router, "sketch");

void onMidi(void *, const espmidi::Message &message)
{
  if (message.chunk)
  {
    return;
  }
  Serial.print("in:  ");
  Serial.print(message.status, HEX);
  Serial.print(" ");
  Serial.print(message.data1);
  Serial.print(" ");
  Serial.println(message.data2);
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  startStack();
  startPort();

  // Watch what arrives.
  // en: Register source and destination endpoints/groups, considering disconnects and unintended loops.
  // ja: 切断と意図しないループを考慮して、入力と出力のEndpoint／グループを登録します。
  router.addRoute(port.in(), sketch.out());
  sketch.onMessage(onMidi);

  // Echo it back an octave up. The transposition is declared, not coded: it
  // belongs to the route, so it applies to every note without a callback.
  const espmidi::Route echo = router.addRoute(port.in(), port.out());
  router.setRouteAllowSameEndpoint(echo, true); // deliberately back where it came from

  espmidi::Filter notesOnly;
  notesOnly.kinds = espmidi::KindNotes;
  router.setRouteFilter(echo, notesOnly);

  espmidi::Transform octaveUp;
  octaveUp.transpose = 12;
  router.setRouteTransform(echo, octaveUp);

  Serial.println("ready");
}

void loop()
{
  updateStack();
  port.update();
  router.update();
}
