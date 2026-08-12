// The same MIDI code over UART, USB or BLE — change one line.
//
// This sketch does one thing: it echoes every note it receives back an octave
// higher, and prints what it saw. What is worth looking at is not the echo but
// **how little changes when the interface changes**.
//
// Change MIDI_PORT below and rebuild. Everything from "the MIDI part" down stays
// exactly as it is:
//
//   MIDI_PORT_UART  a MIDI DIN socket on a UART
//   MIDI_PORT_USB   a USB MIDI port a PC sees
//   MIDI_PORT_BLE   a BLE MIDI service a phone connects to
//
// **That is the reason to use this library even with a single interface.** The
// transport-specific part is starting the stack; a route, a filter and a transform
// are written once and mean the same thing on all three.

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
