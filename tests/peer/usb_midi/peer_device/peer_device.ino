// Peer side of the USB Device MIDI port test: EspMidi's USB Device port on an
// asymmetric multi-cable EspUsbDevice.
//
// **Asymmetric on purpose.** EspUsbDevice names its cable counts from the host's
// point of view, which is the opposite of this library's port directions, so a
// port that swapped them could not be caught by a symmetric device — nor by any
// round trip, since a received packet's cable number comes out of its own header.
// The counts have to differ for "which count is which direction" to mean anything.
//
//   EspUsbDeviceMidi(device, 2, 3)      2 device->host, 3 host->device
//   espmidi::UsbDevicePort              2 output ports,  3 input ports
//
// The DUT side reads the counts back with EspUsbHost::getMidiPortInfo(), which is
// the only check that proves the descriptor really advertised them.
//
// Kept small deliberately: the ESP-IDF USB Host refuses a configuration
// descriptor longer than its enumeration control transfer, and that limit is 256
// bytes in the precompiled Arduino libraries. 16 cables per direction is legal USB
// and works against a PC, but not through this rig.

#include <EspMidiEspUsbDevice.h>

// Host-view, as EspUsbDevice names them: IN is device to host (what this board
// sends), OUT is host to device (what it receives).
static constexpr uint8_t IN_CABLES = 2;
static constexpr uint8_t OUT_CABLES = 3;

EspUsbDevice device;
EspUsbDeviceMidi usbMidi(device, IN_CABLES, OUT_CABLES);

espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::UsbDevicePort midiPort(router, usbMidi, device);
espmidi::AppPort app(router, "test");

uint8_t g_sysex[16] = {};
size_t g_sysexLength = 0;
bool g_greeted = false;
uint32_t g_nextBanner = 0;

void print2(uint8_t value)
{
  if (value < 0x10)
  {
    Serial.print("0");
  }
  Serial.print(value, HEX);
}

// Which input port a message came in on. The message keeps its source port all
// the way through routing, so the cable it arrived on is still readable here.
int portIndexOf(espmidi::PortId port)
{
  for (uint8_t cable = 0; cable < midiPort.inPortCount(); cable++)
  {
    if (midiPort.in(cable).port == port)
    {
      return cable;
    }
  }
  return -1;
}

void onMessage(void *, const espmidi::Message &message)
{
  if (message.chunk)
  {
    if (message.chunkStart)
    {
      g_sysexLength = 0;
    }
    for (size_t i = 0; i < message.chunkLength && g_sysexLength < sizeof(g_sysex); i++)
    {
      g_sysex[g_sysexLength++] = message.chunkData[i];
    }
    if (message.chunkEnd)
    {
      Serial.print("RX_SYSEX port=");
      Serial.print(portIndexOf(message.port));
      for (size_t i = 0; i < g_sysexLength; i++)
      {
        Serial.print(" ");
        print2(g_sysex[i]);
      }
      Serial.println();
    }
    return;
  }

  Serial.print("RX port=");
  Serial.print(portIndexOf(message.port));
  Serial.print(" ");
  print2(message.status);
  for (uint8_t i = 0; i < message.dataLength; i++)
  {
    Serial.print(" ");
    print2(i == 0 ? message.data1 : message.data2);
  }
  Serial.println();
}

// A route per output port, so the sketch can pick which port a message leaves on
// by picking which route it feeds.
espmidi::Route g_routes[espmidi::MaxPortsPerEndpoint];

void sendOn(uint8_t outPort, uint8_t status, uint8_t d1, uint8_t d2)
{
  for (uint8_t i = 0; i < midiPort.outPortCount(); i++)
  {
    router.setRouteEnabled(g_routes[i], i == outPort);
  }
  app.sendShort(status, d1, d2);
  router.update();
  Serial.print("TX port=");
  Serial.println(outPort);
}

void sendSysExOn(uint8_t outPort)
{
  for (uint8_t i = 0; i < midiPort.outPortCount(); i++)
  {
    router.setRouteEnabled(g_routes[i], i == outPort);
  }
  const uint8_t payload[] = {0x7d, 0x21, 0x22};
  espmidi::Message dump;
  dump.type = espmidi::MessageType::Data7;
  dump.status = 0xf0;
  dump.chunk = true;
  dump.chunkStart = true;
  dump.chunkEnd = true;
  dump.chunkData = payload;
  dump.chunkLength = sizeof(payload);
  app.send(dump);
  router.update();
  Serial.print("TX_SYSEX port=");
  Serial.println(outPort);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  // 1) The stack. EspMidi does not own it.
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4017;
  config.manufacturer = "EspMidi";
  config.product = "EspMidi USB MIDI";
  device.begin(config);

  // 2) The ports. The cable counts are final once the stack is up.
  midiPort.begin("USB MIDI");

  // 3) The routes. Every input port is watched; one route per output port.
  router.addRoute(espmidi::InGroup::all(), app.out());
  app.onMessage(onMessage);
  for (uint8_t i = 0; i < midiPort.outPortCount(); i++)
  {
    g_routes[i] = router.addRoute(app.in(), midiPort.out(i));
    router.setRouteEnabled(g_routes[i], false);
  }
}

void loop()
{
  if (!g_greeted && static_cast<int32_t>(millis() - g_nextBanner) >= 0)
  {
    Serial.println("PEER_READY");
    g_nextBanner = millis() + 500;
  }

  if (Serial.available() > 0)
  {
    g_greeted = true;
    switch (Serial.read())
    {
      case '?':
        // This library's own view of the ports, which is the inversion of the
        // cable counts the DUT reads out of the descriptor.
        Serial.print("PORTS in=");
        Serial.print(midiPort.inPortCount());
        Serial.print(" out=");
        Serial.println(midiPort.outPortCount());
        break;
      case '0':
        sendOn(0, 0x90, 0x40, 0x50);
        break;
      case '1':
        sendOn(1, 0xb0, 0x0b, 0x20);
        break;
      case 's':
        sendSysExOn(1);
        break;
      case 'u':
        Serial.print("UNKNOWN_CABLE ");
        Serial.println(midiPort.unknownCablePackets());
        break;
      default:
        break;
    }
  }

  device.task();
  midiPort.update();
  router.update();
}
