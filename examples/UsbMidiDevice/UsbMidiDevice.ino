// USB MIDI interface with two ports.
//
// Plug the board into a PC and two MIDI ports appear. Port 1 carries whatever
// arrives on the UART MIDI IN, and anything the PC sends to port 1 goes out of the
// UART MIDI OUT — a USB MIDI interface for one MIDI DIN pair. Port 2 is the
// board's own: the sketch plays notes on it and watches what the PC sends back.
//
// That second port is what a USB MIDI interface for sale cannot do and this one
// can: the board is a MIDI device and a MIDI router at the same time, and both
// halves are just ports to the routing.
//
// Wiring: RX_PIN to the MIDI IN, TX_PIN to the MIDI OUT. A real MIDI DIN socket
// needs an optocoupler on the input and a 220Ω pair on the output; that is outside
// this library, which deals in bytes.
//
// The console is the board's external USB-serial chip, separate from the native
// USB the PC sees.

#include <EspMidiEspUsbDevice.h>
#include <EspMidiUart.h>

static const int8_t RX_PIN = 20;
static const int8_t TX_PIN = 19;

// A button to play with, so the second port has something to send.
static const uint8_t BUTTON_PIN = 0;

// Host-view, as EspUsbDevice names them: IN is device to host, OUT is host to
// device. Two each, so the PC sees two ports in both directions.
static constexpr uint8_t IN_CABLES = 2;
static constexpr uint8_t OUT_CABLES = 2;

EspUsbDevice usb;
EspUsbDeviceMidi usbMidi(usb, IN_CABLES, OUT_CABLES);

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UsbDevicePort usbPort(router, usbMidi, usb);
espmidi::UartPort uart(router, Serial1, 1);

// The board's own port. It has no transport behind it and routes like any other.
espmidi::AppPort local(router, "sketch");

void printMessage(void *, const espmidi::Message &message)
{
  if (message.chunk)
  {
    return; // a data stream; nothing to say about it here
  }

  Serial.print("from the PC: ");
  Serial.print(message.status, HEX);
  Serial.print(" ");
  Serial.print(message.data1);
  Serial.print(" ");
  Serial.println(message.data2);
}

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(200);

  // 1) The stacks. EspMidi owns neither of them.
  EspUsbDeviceConfig config;
  config.manufacturer = "EspMidi";
  config.product = "EspMidi Interface";
  usb.begin(config);

  // 2) The ports. The USB port's cable counts are final once the stack is up, so
  //    begin() comes after usb.begin().
  usbPort.begin("USB MIDI");
  uart.begin("MIDI DIN", RX_PIN, TX_PIN);

  // 3) The routes. USB port 1 is the MIDI DIN pair, in both directions.
  router.addRoute(uart.in(), usbPort.out(0));
  router.addRoute(usbPort.in(0), uart.out());

  // USB port 2 is the board itself.
  router.addRoute(local.in(), usbPort.out(1));
  router.addRoute(usbPort.in(1), local.out());
  local.onMessage(printMessage);

  Serial.println("EspMidi USB MIDI interface ready");
}

void loop()
{
  usb.task();
  usbPort.update();
  uart.update();
  router.update();

  // The button plays middle C on USB port 2. Nothing debounces it; a real control
  // mapping helper is Phase 9.
  static bool wasDown = false;
  const bool isDown = digitalRead(BUTTON_PIN) == LOW;
  if (isDown != wasDown)
  {
    wasDown = isDown;
    local.sendShort(isDown ? 0x90 : 0x80, 60, isDown ? 100 : 0);
  }

  // A port the PC has not configured yet refuses what it is handed, and routing
  // counts it. It is the first thing to look at when nothing seems to arrive.
  static uint32_t reported = 0;
  const uint32_t failed = router.counters().sendFailed;
  if (failed != reported)
  {
    reported = failed;
    Serial.print("send failures: ");
    Serial.println(failed);
  }
}
