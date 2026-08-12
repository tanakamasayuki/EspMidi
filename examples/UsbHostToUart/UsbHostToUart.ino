// A USB MIDI keyboard playing a MIDI DIN sound module, and a PC listening in.
//
// Plug a USB MIDI keyboard into the board's USB host port and it plays the sound
// module on the MIDI DIN out — no PC involved. Plug the board into a PC as well
// and the same performance also arrives there, so a DAW can record it while the
// module is being played.
//
// This is UC1 in docs/USE_CASES.ja.md, and the point of the library in one sketch:
// three transports that know nothing about each other, joined by routes.
//
//   USB MIDI keyboard ──→ ┐
//                         ├──→ MIDI DIN OUT (sound module)
//                         └──→ PC (USB Device port 1)
//   PC (port 1) ──────────────→ MIDI DIN OUT as well
//
// **Nothing names the keyboard.** Its seats appear when it is plugged in, so the
// routes are written from InGroup::all() and whatever arrives takes part. Unplug
// it mid-phrase and the routes stay; plug it back in and it carries on.
//
// Hardware: an ESP32-P4, or an S3 with a USB host port wired up. The console is
// the board's external USB-serial chip.

#include <EspMidiEspUsbDevice.h>
#include <EspMidiEspUsbHost.h>
#include <EspMidiUart.h>

static const int8_t RX_PIN = 20;
static const int8_t TX_PIN = 19;

EspUsbHost usbHost;
EspUsbDevice usbDevice;
EspUsbDeviceMidi usbDeviceMidi(usbDevice, 1, 1);

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UsbHostPort keyboards(router, usbHost);
espmidi::UsbDevicePort pc(router, usbDeviceMidi, usbDevice);
espmidi::UartPort din(router, Serial1, 1);

// Reports what appears and disappears. A seat is never removed, so there are only
// two things to hear about.
void onPortEvent(void *, const espmidi::PortEvent &event)
{
  espmidi::PortInfo info;
  if (!registry.portInfo(event.port, info))
  {
    return;
  }
  Serial.print(event.type == espmidi::PortEventType::PortAdded ? "port added: " : "port state: ");
  Serial.print(info.name);
  Serial.print(info.direction == espmidi::Direction::In ? " (in) " : " (out) ");
  Serial.println(info.state == espmidi::PortState::Available ? "available" : "disconnected");
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  // 1) The stacks. EspMidi owns none of them.
  usbHost.begin();
  EspUsbDeviceConfig config;
  config.manufacturer = "EspMidi";
  config.product = "EspMidi Bridge";
  usbDevice.begin(config);

  // 2) The ports. The USB host port supplies nothing yet — it has not met a
  //    device. The other two are here from the start.
  keyboards.begin();
  pc.begin("PC");
  din.begin("MIDI DIN", RX_PIN, TX_PIN);

  // 3) The routes. Written against every input rather than against a device,
  //    because the keyboard does not exist yet.
  //
  //    A route never sends a message back to the endpoint it came from, so
  //    "everything to the module and to the PC" cannot make the PC echo to itself
  //    or the DIN input loop back to its own output.
  router.addRoute(espmidi::InGroup::all(), din.out());
  router.addRoute(espmidi::InGroup::all(), pc.out(0));

  registry.addListener(onPortEvent);
  Serial.println("EspMidi bridge ready — plug in a USB MIDI keyboard");
}

void loop()
{
  usbDevice.task();
  keyboards.update();
  pc.update();
  din.update();
  router.update();
}
