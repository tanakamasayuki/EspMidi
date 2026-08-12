// DUT side of the USB Host MIDI port test: EspMidi's USB Host port watching the
// board that runs EspMidi's USB Device port.
//
// Both ends are EspMidi here, which is what makes this the interesting test and
// also what makes tests/peer/usb_midi worth keeping: there the device half is
// watched by a plain EspUsbHost, so a wrong cable nibble cannot cancel out. This
// one adds what that cannot show — the dynamic side. Seats appear when a device is
// plugged in, without the sketch naming it first.
//
// The two boards report mirror-image port counts:
//
//   this side (host)    in=2 out=3     EspUsbHost's counts are already host-view
//   peer side (device)  in=3 out=2     the same counts, inverted for a device
//
// Nothing in the sketch mentions a device in advance. The route is from
// InGroup::all(), so whatever appears takes part.

#include <EspMidiEspUsbHost.h>

EspUsbHost usb;

espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::UsbHostPort hostPort(router, usb);
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

// Which device and cable a message came in on. The source port travels with the
// message all the way through routing, so it is still readable here.
void printSource(espmidi::PortId port)
{
  for (size_t i = 0; i < hostPort.deviceCount(); i++)
  {
    const uint8_t address = hostPort.addressAt(i);
    for (uint8_t cable = 0; cable < hostPort.inPortCount(address); cable++)
    {
      if (hostPort.in(address, cable).port == port)
      {
        Serial.print(" port=");
        Serial.print(cable);
        return;
      }
    }
  }
  Serial.print(" port=?");
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
      Serial.print("RX_SYSEX");
      printSource(message.port);
      for (size_t i = 0; i < g_sysexLength; i++)
      {
        Serial.print(" ");
        print2(g_sysex[i]);
      }
      Serial.println();
    }
    return;
  }

  Serial.print("RX");
  printSource(message.port);
  Serial.print(" ");
  print2(message.status);
  for (uint8_t i = 0; i < message.dataLength; i++)
  {
    Serial.print(" ");
    print2(i == 0 ? message.data1 : message.data2);
  }
  Serial.println();
}

// Sends on one of the connected device's output ports. The route is built when it
// is needed rather than up front, because there is nothing to point it at until a
// device has arrived.
void sendOn(uint8_t outPort, const espmidi::Message &message)
{
  if (hostPort.deviceCount() == 0)
  {
    Serial.println("TX no device");
    return;
  }
  const uint8_t address = hostPort.addressAt(0);
  const espmidi::OutPort out = hostPort.out(address, outPort);
  if (!out.valid())
  {
    Serial.print("TX no port ");
    Serial.println(outPort);
    return;
  }

  const espmidi::Route route = router.addRoute(app.in(), out);
  app.send(message);
  router.update();
  router.removeRoute(route);

  Serial.print("TX port=");
  Serial.println(outPort);
}

void sendShortOn(uint8_t outPort, uint8_t status, uint8_t d1, uint8_t d2)
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message message;
  espmidi::buildShortMessage(message, bytes, status, d1, d2);
  sendOn(outPort, message);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  // 1) The stack. EspMidi does not own it.
  usb.begin();

  // 2) The port. It supplies nothing yet: seats appear as devices are found.
  hostPort.begin();

  // 3) The routes. Whatever appears is watched, without being named.
  router.addRoute(espmidi::InGroup::all(), app.out());
  app.onMessage(onMessage);
}

void loop()
{
  if (!g_greeted && static_cast<int32_t>(millis() - g_nextBanner) >= 0)
  {
    Serial.println("HOST_READY");
    g_nextBanner = millis() + 500;
  }

  if (Serial.available() > 0)
  {
    g_greeted = true;
    switch (Serial.read())
    {
      case 'd':
      {
        // Waited for rather than sampled: enumeration and the port's own poll
        // both take longer than the test's question does.
        const uint32_t until = millis() + 5000;
        while (hostPort.deviceCount() == 0 && static_cast<int32_t>(until - millis()) > 0)
        {
          hostPort.update();
          router.update();
          delay(10);
        }
        Serial.print("DEVICES ");
        Serial.println(hostPort.deviceCount());
        for (size_t i = 0; i < hostPort.deviceCount(); i++)
        {
          const uint8_t address = hostPort.addressAt(i);
          Serial.print("DEVICE in=");
          Serial.print(hostPort.inPortCount(address));
          Serial.print(" out=");
          Serial.println(hostPort.outPortCount(address));
        }
        break;
      }
      case 'n':
        // Output port 2 exists only because the device declared 3 host-to-device
        // cables. It is the port a mixed-up direction would not have.
        sendShortOn(2, 0x90, 0x3c, 0x64);
        break;
      case 'c':
        sendShortOn(0, 0xb0, 0x07, 0x40);
        break;
      case 's':
      {
        const uint8_t payload[] = {0x7d, 0x11, 0x12};
        espmidi::Message dump;
        dump.type = espmidi::MessageType::Data7;
        dump.status = 0xf0;
        dump.chunk = true;
        dump.chunkStart = true;
        dump.chunkEnd = true;
        dump.chunkData = payload;
        dump.chunkLength = sizeof(payload);
        sendOn(1, dump);
        break;
      }
      case 'x':
        Serial.print("DIAG unknown=");
        Serial.print(hostPort.unknownCablePackets());
        Serial.print(" dropped=");
        Serial.print(hostPort.droppedPackets());
        Serial.print(" refused=");
        Serial.println(hostPort.refusedDevices());
        break;
      default:
        break;
    }
  }

  hostPort.update();
  router.update();
}
