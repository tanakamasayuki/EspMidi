// UART MIDI between two boards — the DUT half.
//
// The peer boards are already wired for the USB tests: GPIO19 to GPIO19, GPIO20
// to GPIO20, and a common ground, straight through. A MIDI link needs a
// crossover, and this one is made by giving the two roles opposite pins rather
// than by rewiring anything:
//
//                DUT                 peer
//   GPIO19       TX      ────────    RX
//   GPIO20       RX      ────────    TX
//
// At 31250 baud the series resistors in the USB data path are not a problem. The
// one requirement is that neither sketch uses the native USB peripheral, since
// that is what those pins belong to — the console here is UART0, the board's
// external USB-serial chip.
//
// What this proves that loopback/uart_midi does not: the byte crosses between
// two separate clock domains, so the receiver is resolving the bit timing of a
// signal it did not generate.

#include <EspMidiUart.h>

static const int8_t TX_PIN = 19;
static const int8_t RX_PIN = 20;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UartPort linkPort(router, Serial1, 1);
espmidi::AppPort app(router, "test");

uint8_t g_sysex[16] = {};
size_t g_sysexLength = 0;

// The board is reset by the flashing tool and the console is opened after that,
// so a banner printed once in setup() is gone before anyone is listening. It is
// repeated until the first byte arrives from the console instead.
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

  Serial.print("RX ");
  print2(message.status);
  for (uint8_t i = 0; i < message.dataLength; i++)
  {
    Serial.print(" ");
    print2(i == 0 ? message.data1 : message.data2);
  }
  Serial.println();
}

void sendSysEx()
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
  app.send(dump);
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  linkPort.begin("peer link", RX_PIN, TX_PIN);

  router.addRoute(app.in(), linkPort.out());
  router.addRoute(linkPort.in(), app.out());
  app.onMessage(onMessage);
}

void loop()
{
  if (!g_greeted && static_cast<int32_t>(millis() - g_nextBanner) >= 0)
  {
    Serial.println("DUT_READY");
    g_nextBanner = millis() + 500;
  }

  if (Serial.available() > 0)
  {
    g_greeted = true;
    switch (Serial.read())
    {
      case '?':
        Serial.println("DUT_READY");
        break;
      case 'n':
        app.sendShort(0x90, 0x3c, 0x64);
        Serial.println("TX_NOTE_ON");
        break;
      case 'c':
        app.sendShort(0xb0, 0x07, 0x40);
        Serial.println("TX_CC");
        break;
      case 's':
        sendSysEx();
        Serial.println("TX_SYSEX");
        break;
      default:
        break;
    }
  }

  linkPort.update();
  router.update();
}
