// DUT side of the BLE MIDI test: EspMidi's BLE Host port.
//
// The board scans for the BLE MIDI service, connects, and gets a seat for the
// connection. The sketch does the scanning and connecting — that is BLE, which
// EspMidi does not own — and stops there: finding the MIDI service on the
// connection is the port's job, so the sketch never calls discover().
//
// Nothing here names the peer. The route is from InGroup::all(), so the seat that
// appears when the connection becomes usable takes part on its own.

#include <EspMidiEspBle.h>

EspBle ble;
EspBleMidiHost bleMidi(ble);

espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::BleHostPort midiPort(router, bleMidi, ble);
espmidi::AppPort app(router, "test");

uint8_t g_sysex[64] = {};
size_t g_sysexLength = 0;
bool g_greeted = false;
bool g_connectRequested = false;
uint32_t g_nextBanner = 0;

void print2(uint8_t value)
{
  if (value < 0x10)
  {
    Serial.print("0");
  }
  Serial.print(value, HEX);
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
      for (size_t i = 0; i < g_sysexLength; i++)
      {
        Serial.print(" ");
        print2(g_sysex[i]);
      }
      Serial.println();
    }
    return;
  }

  Serial.print("RX ");
  print2(message.status);
  for (uint8_t i = 0; i < message.dataLength; i++)
  {
    Serial.print(" ");
    print2(i == 0 ? message.data1 : message.data2);
  }
  Serial.print(" unit=");
  Serial.println(static_cast<int>(message.timestamp.unit));
}

// The route is built when it is needed: there is nothing to point one at until a
// connection has a seat.
void sendTo(const espmidi::Message &message)
{
  if (midiPort.deviceCount() == 0)
  {
    Serial.println("TX no device");
    return;
  }
  const espmidi::OutPort out = midiPort.out(midiPort.connectionAt(0));
  if (!out.valid())
  {
    Serial.println("TX no port");
    return;
  }
  const espmidi::Route route = router.addRoute(app.in(), out);
  app.send(message);
  router.update();
  router.removeRoute(route);
  Serial.println("TX ok");
}

void sendShort(uint8_t status, uint8_t d1, uint8_t d2)
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message message;
  espmidi::buildShortMessage(message, bytes, status, d1, d2);
  sendTo(message);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  // 1) The stack, and the scanning that finds a peer. Connecting is the sketch's
  //    business; what is on the connection is the port's.
  ble.begin();
  bleMidi.begin();
  ble.scanner().onResult([](const EspBleScanResult &result) {
    if (g_connectRequested || !result.advertisesService(ESP_BLE_MIDI_SERVICE_UUID))
    {
      return;
    }
    ble.scanner().stop();
    g_connectRequested = ble.connect(result);
  });

  // 2) The port. It supplies nothing yet.
  midiPort.begin();

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
      case 'z':
      {
        EspBleScanConfig scan;
        scan.active = true;
        Serial.println(ble.scanner().start(scan) ? "SCAN_STARTED" : "SCAN_FAILED");
        break;
      }
      case 'r':
        Serial.print("DEVICES ");
        Serial.println(midiPort.deviceCount());
        break;
      case 'n':
        sendShort(0x90, 0x3c, 0x64);
        break;
      case 'c':
        sendShort(0xb0, 0x07, 0x40);
        break;
      case 'y':
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
        sendTo(dump);
        break;
      }
      case 'x':
        Serial.print("DIAG oversized=");
        Serial.print(midiPort.oversizedStreams());
        Serial.print(" events=");
        Serial.print(midiPort.droppedEvents());
        Serial.print(" refused=");
        Serial.println(midiPort.refusedConnections());
        break;
      default:
        break;
    }
  }

  ble.update();
  midiPort.update();
  router.update();
}
