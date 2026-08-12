// Peer side of the BLE MIDI test: EspMidi's BLE Device port.
//
// The board advertises the BLE MIDI service and waits to be connected to. Nothing
// here touches GATT: the sketch owns the BLE stack and hands EspMidi an
// EspBleMidiDevice that is already registered.
//
// Note the order in setup(). The MIDI GATT service has to be registered before
// the server starts, so midi.begin() comes before ble.begin(), and the EspMidi
// port comes after both — it needs the profile object to exist, not the link.

#include <EspMidiEspBle.h>

EspBle ble;
EspBleMidiDevice bleMidi(ble);

espmidi::PortRegistry registry;
espmidi::Router router(registry);
espmidi::BleDevicePort midiPort(router, bleMidi);
espmidi::AppPort app(router, "test");

uint8_t g_sysex[64] = {};
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

  // The timestamp is printed because BLE MIDI is the only transport that has one,
  // and the test checks that it arrives labelled rather than as a bare number.
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

void sendSysEx()
{
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
  Serial.println("TX_SYSEX");
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  // 1) The stack. The GATT service has to be registered before the server starts.
  bleMidi.begin();
  EspBleConfig config;
  config.deviceName = "EspMidi BLE Peer";
  ble.begin(config);
  ble.advertising().setName("EspMidi BLE Peer");
  ble.advertising().start();

  // 2) The port.
  midiPort.begin("BLE MIDI");

  // 3) The routes.
  router.addRoute(midiPort.in(), app.out());
  router.addRoute(app.in(), midiPort.out());
  app.onMessage(onMessage);
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
        Serial.print("ADVERTISING ");
        Serial.println(ble.advertising().isAdvertising() ? 1 : 0);
        break;
      case 'r':
        // Available means a host has subscribed, which is when the seat can
        // carry anything.
        Serial.print("PEER_AVAILABLE ");
        Serial.println(midiPort.available() ? 1 : 0);
        break;
      case '0':
        app.sendShort(0x90, 0x40, 0x50);
        router.update();
        Serial.println("TX_NOTE");
        break;
      case '1':
        app.sendShort(0xb0, 0x0b, 0x20);
        router.update();
        Serial.println("TX_CC");
        break;
      case 'y':
        sendSysEx();
        break;
      case 'x':
        Serial.print("DIAG oversized=");
        Serial.print(midiPort.oversizedStreams());
        Serial.print(" failed=");
        Serial.println(router.counters().sendFailed);
        break;
      default:
        break;
    }
  }

  ble.update();
  midiPort.update();
  router.update();
}
