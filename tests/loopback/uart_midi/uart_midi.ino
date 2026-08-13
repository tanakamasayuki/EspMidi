// UART MIDI loopback on one board, with no wiring at all.
//
// Two real UART peripherals are used: UART1 transmits and UART2 receives, and
// the GPIO matrix puts both on the same pin. Nothing is soldered, nothing is
// jumpered, and the bytes still go out of one peripheral's shifter and into
// another's at 31250 baud.
//
//   espmidi::UartPort a  ──→ UART1_TX ─┐
//                                      ├─ SHARED_PIN (no wire)
//   espmidi::UartPort b  ←── UART2_RX ─┘
//
// The UART peripheral's own internal loopback (uart_set_loop_back()) would also
// need no wiring, but it never leaves one peripheral. This way the byte crosses
// the pad, which is the part a unit test cannot reach: unit/uart_port already
// fixes everything on either side of it.
//
// The two ports are connected through EspMidi rather than by hand — an
// application port injects into A and receives from B — so what this proves is
// the whole path a sketch actually uses.

#include <EspMidiUart.h>

#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

// The pin both peripherals sit on. It has to be one the UART cannot reach
// through the IOMUX directly, or the matrix would be bypassed; on the ESP32-S3
// only UART0 has IOMUX pins, so any free GPIO works. Nothing is connected to it.
static const int8_t SHARED_PIN = 21;

// UART1 has to be given some transmit pin by the Arduino layer before the matrix
// is redirected. Nothing is connected here either, and nothing is sent to it.
static const int8_t PARKING_PIN = 14;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UartPort a(router, Serial1, 1);
espmidi::UartPort b(router, Serial2, 2);
espmidi::AppPort app(router, "test");

int g_failures = 0;
uint8_t g_sysex[16] = {};
size_t g_sysexLength = 0;

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
      // Printed only when the stream ends: how the dump was split into chunks
      // depends on how the bytes happened to land in one read, and that is not
      // something the wire is supposed to guarantee.
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

// Puts UART1's transmit signal onto the pin UART2 is already listening to.
//
// The Arduino layer tracks one peripheral per pin and detaches the previous one
// when a second claims it, so the second connection is made through the matrix
// directly. Connecting an output signal also enables the pin's output driver,
// while the input path UART2 set up stays as it was — which is exactly the
// arrangement wanted here.
//
// **This connection outlives the sketch.** The GPIO matrix is registers, and a
// soft reset does not clear them, so after this test the board can still have
// UART1's transmit signal on SHARED_PIN while the Arduino core believes nothing
// is assigned (uart_get_TxPin() reports -1). The next sketch then transmits on a
// pin it never asked for. Power the board down between this test and anything
// that measures pins.
void shareThePin()
{
  esp_rom_gpio_connect_out_signal(SHARED_PIN, U1TXD_OUT_IDX, false, false);
}

void expect(bool condition, const char *what)
{
  if (!condition)
  {
    g_failures++;
    Serial.print("FAIL ");
    Serial.println(what);
  }
}

void pump(uint32_t milliseconds)
{
  const uint32_t until = millis() + milliseconds;
  while (static_cast<int32_t>(until - millis()) > 0)
  {
    a.update();
    b.update();
    router.update();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  // The receiving side claims the pin first, so its input routing is the one the
  // Arduino layer knows about.
  expect(b.begin("uart b", SHARED_PIN, -1), "b.begin");
  expect(a.begin("uart a", -1, PARKING_PIN), "a.begin");
  shareThePin();

  router.addRoute(app.in(), a.out());
  router.addRoute(b.in(), app.out());
  app.onMessage(onMessage);

  expect(registry.portCount() == 6, "two UART ports and one application port");
}

void runTest()
{
  // One message per step, with time in between: at 31250 baud three bytes take
  // about a millisecond, and separating them keeps a failure readable.
  app.sendShort(0x90, 0x3c, 0x64);
  pump(20);
  app.sendShort(0xb0, 0x07, 0x40);
  pump(20);
  app.sendShort(0xf8);
  pump(20);
  app.sendShort(0xe0, 0x00, 0x40);
  pump(20);

  // A dump. The payload is what a chunk carries; the 0xF0 and 0xF7 around it are
  // the serializer's, and the parser on the other side takes them off again.
  const uint8_t payload[] = {0x7d, 0x01, 0x02};
  espmidi::Message dump;
  dump.type = espmidi::MessageType::Data7;
  dump.status = 0xf0;
  dump.chunk = true;
  dump.chunkStart = true;
  dump.chunkEnd = true;
  dump.chunkData = payload;
  dump.chunkLength = sizeof(payload);
  expect(app.send(dump), "send dump");
  pump(50);

  app.sendShort(0x80, 0x3c, 0x00);
  pump(50);

  const espmidi::RouterCounters &counters = router.counters();
  expect(counters.sendFailed == 0, "nothing was refused by the transport");
  expect(counters.queueFull == 0, "the queue never overflowed");
  expect(counters.noRoute == 0, "every message had somewhere to go");

  Serial.print("TEST_END ");
  Serial.println(g_failures == 0 ? "ok" : "failed");
  Serial.println(g_failures == 0 ? "OK" : "NG");
}

// The banner is repeated rather than printed once, and the run waits to be asked
// for. The board is reset by the flashing tool and the console is opened after
// that, so anything printed in the first moments of setup() is gone before
// anyone is listening.
void loop()
{
  // The one character the test sends, not just any byte: a board's UART0 can pick
  // up a stray one as the flashing tool releases the line, and starting the run on
  // that would mean the console is opened after everything has already been said.
  if (Serial.available() > 0 && Serial.read() == 'g')
  {
    runTest();
    while (true)
    {
      pump(1000);
    }
  }

  Serial.println("LOOPBACK_READY");
  pump(500);
}
