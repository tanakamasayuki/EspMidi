// A USB MIDI interface and a MIDI DIN circuit, joined into one loop.
//
// One board runs both ports. A commodity USB MIDI interface is plugged into the
// USB host connector, and its two DIN jacks are wired back to this board's own
// MIDI IN and MIDI OUT circuits, so the loop closes through real hardware:
//
//   EspMidi UartPort  ──→ TX pin ──[MIDI OUT circuit]──→ interface DIN IN
//                                                              │  (USB)
//   EspMidi UsbHostPort ←──────────────────────────────────────┘
//
//   EspMidi UsbHostPort ──→ (USB) ──→ interface DIN OUT
//                                          │  [MIDI IN circuit]
//   EspMidi UartPort  ←── RX pin ←─────────┘
//
// **The two directions travel over different physical layers.** What leaves on
// the DIN comes back over USB and what leaves over USB comes back on the DIN, so
// a single run exercises the UART port and the USB host port against each other
// inside one router — through a real optocoupler and a third party's firmware.
//
// The GPIO numbers are not compiled in. The circuit is wired differently on every
// rig, so the pins arrive over the console before anything is started:
//
//   host → board   "p 17 18"     transmit pin, receive pin
//   board → host   "PINS tx=17 rx=18"
//   board → host   "USB_DEVICE in=1 out=1"
//   host → board   "g"           run
//
// Hardware: an ESP32-S3 with a USB host connector (the interface needs 5 V), the
// MIDI IN and MIDI OUT circuits of docs/HARDWARE.ja.md, and two MIDI cables. The
// console is the board's external USB-serial chip, because the USB peripheral is
// spent on the host side.

#include <EspMidiEspUsbHost.h>
#include <EspMidiUart.h>

EspUsbHost usb;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UartPort din(router, Serial1, 1);
espmidi::UsbHostPort adapter(router, usb);

// Three application ports rather than one, because the direction a message takes
// has to be chosen per message. A single port routed to both outputs would send
// everything both ways at once, and then neither arrival would say which path it
// came from.
espmidi::AppPort toDin(router, "to din");
espmidi::AppPort toUsb(router, "to usb");
espmidi::AppPort sink(router, "sink");

int g_failures = 0;
bool g_configured = false;

// The routes this sketch built, so that running "p" again replaces them instead
// of adding a second set. addRoute() is not idempotent — the registry's seats are,
// which makes the difference easy to miss — and a duplicated route means the same
// message is sent twice and delivered twice.
espmidi::Route g_routes[2 + ESPMIDI_MAX_PORTS] = {};
size_t g_routeCount = 0;

bool g_dinStarted = false;
uint8_t g_address = 0;

uint8_t g_sysex[16] = {};
size_t g_sysexLength = 0;

// What the last step is waiting for. The callback runs inside router.update(),
// which is inside the pump loop, so a step can stop as soon as it is satisfied
// instead of always spending its whole timeout.
const char *g_awaited = nullptr;
bool g_arrived = false;

// When the last bring-up command transmitted, so every arrival can say how long
// it took to come back. It is the measurement that separates an echo from a relay
// without an instrument: a copy that is electrically the same wire returns within
// the message itself, while anything that decodes and re-sends cannot.
uint32_t g_txMicros = 0;

// Arrivals by path, counted whatever the current step is waiting for. The rig
// check needs to know what came back without the noise of a failed await.
uint32_t g_dinArrivals = 0;
uint32_t g_usbArrivals = 0;

char g_line[32] = {};
size_t g_lineLength = 0;

uint32_t g_nextBanner = 0;
bool g_greeted = false;

void print2(uint8_t value)
{
  if (value < 0x10)
  {
    Serial.print("0");
  }
  Serial.print(value, HEX);
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
    adapter.update();
    din.update();
    router.update();
  }
}

// Waits for what the step expects, and gives up after the timeout. Nothing here
// is fast: a USB MIDI interface buffers, and 31250 baud is 0.32 ms a byte.
bool await(const char *label, uint32_t timeoutMs)
{
  g_awaited = label;
  g_arrived = false;
  const uint32_t until = millis() + timeoutMs;
  while (!g_arrived && static_cast<int32_t>(until - millis()) > 0)
  {
    adapter.update();
    din.update();
    router.update();
  }
  g_awaited = nullptr;
  expect(g_arrived, label);
  return g_arrived;
}

// Every step names the path it expects, and an arrival by the other one does not
// satisfy it. A rig where the board's own MIDI OUT reaches its own MIDI IN — an
// interface that merges its DIN IN into its DIN OUT does exactly that — would
// otherwise answer every step without the far half of the loop working at all.
void arrived(const char *label)
{
  if (g_awaited == nullptr)
  {
    return;
  }
  if (strcmp(g_awaited, label) == 0)
  {
    g_arrived = true;
    return;
  }
  Serial.print("WRONG_PATH want=");
  Serial.print(g_awaited);
  Serial.print(" got=");
  Serial.println(label);
}

void countArrival(espmidi::PortId port)
{
  if (port == din.in().port)
  {
    g_dinArrivals++;
  }
  else
  {
    g_usbArrivals++;
  }
}

void printElapsed()
{
  if (g_txMicros == 0)
  {
    return;
  }
  Serial.print(" +");
  Serial.print(micros() - g_txMicros);
  Serial.print("us");
}

// Which physical layer the message came back over. The source port travels with
// the message through routing, so the answer is still here.
const char *sourceOf(espmidi::PortId port)
{
  return port == din.in().port ? "DIN" : "USB";
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
      // Only at the end: how a dump is split into chunks depends on how the bytes
      // happened to land in one read, and neither wire promises anything there.
      Serial.print("RX_SYSEX_FROM_");
      Serial.print(sourceOf(message.port));
      for (size_t i = 0; i < g_sysexLength; i++)
      {
        Serial.print(" ");
        print2(g_sysex[i]);
      }
      printElapsed();
      Serial.println();
      countArrival(message.port);
      arrived(message.port == din.in().port ? "sysex from DIN" : "sysex from USB");
    }
    return;
  }

  Serial.print("RX_FROM_");
  Serial.print(sourceOf(message.port));
  Serial.print(" ");
  print2(message.status);
  for (uint8_t i = 0; i < message.dataLength; i++)
  {
    Serial.print(" ");
    print2(i == 0 ? message.data1 : message.data2);
  }
  printElapsed();
  Serial.println();
  countArrival(message.port);
  arrived(message.port == din.in().port ? "message from DIN" : "message from USB");
}

void clearRoutes()
{
  for (size_t i = 0; i < g_routeCount; i++)
  {
    router.removeRoute(g_routes[i]);
  }
  g_routeCount = 0;
}

void addRoute(espmidi::Route route)
{
  if (g_routeCount < sizeof(g_routes) / sizeof(g_routes[0]))
  {
    g_routes[g_routeCount++] = route;
  }
}

// "p <tx> <rx>". Parsed rather than compiled in: the pins depend on how the rig
// is built, and rebuilding the sketch for each one would make the pin numbers
// part of the repository instead of part of the bench.
bool parsePins(const char *line, long &txPin, long &rxPin)
{
  if (line[0] != 'p')
  {
    return false;
  }
  char *end = nullptr;
  txPin = strtol(line + 1, &end, 10);
  if (end == line + 1)
  {
    return false;
  }
  const char *rest = end;
  rxPin = strtol(rest, &end, 10);
  return end != rest;
}

// Brings the ports up on the pins the host chose, and waits for the interface to
// enumerate. Nothing names the interface: its seats appear because it was plugged
// in, which is the same thing the USB host port does for any device.
void configure(long txPin, long rxPin)
{
  // Released before being opened again. Re-running "p" with different pins would
  // otherwise leave the previous ones attached — the ESP32 routes a peripheral to
  // a pad through the GPIO matrix, and opening a second pad does not take the
  // first one back. The board then transmits on a pin nobody asked for.
  if (g_dinStarted)
  {
    din.end();
    g_dinStarted = false;
  }

  g_dinStarted = din.begin("MIDI DIN", static_cast<int8_t>(rxPin), static_cast<int8_t>(txPin));
  expect(g_dinStarted, "din.begin");

  usb.begin();
  expect(adapter.begin(), "adapter.begin");

  clearRoutes();
  addRoute(router.addRoute(toDin.in(), din.out()));
  addRoute(router.addRoute(din.in(), sink.out()));
  sink.onMessage(onMessage);

  Serial.print("PINS tx=");
  Serial.print(txPin);
  Serial.print(" rx=");
  Serial.println(rxPin);

  // Enumeration, and then the port's own poll for the MIDI interface, both take
  // longer than the question does.
  const uint32_t until = millis() + 15000;
  while (adapter.deviceCount() == 0 && static_cast<int32_t>(until - millis()) > 0)
  {
    pump(50);
  }
  if (adapter.deviceCount() == 0)
  {
    Serial.println("USB_NONE");
    return;
  }

  g_address = adapter.addressAt(0);
  const uint8_t inCables = adapter.inPortCount(g_address);
  const uint8_t outCables = adapter.outPortCount(g_address);

  // Every cable the interface offers is watched, so a two-jack interface answers
  // on whichever one it chose. Sending uses cable 0.
  for (uint8_t cable = 0; cable < inCables; cable++)
  {
    addRoute(router.addRoute(adapter.in(g_address, cable), sink.out()));
  }
  if (outCables > 0)
  {
    addRoute(router.addRoute(toUsb.in(), adapter.out(g_address, 0)));
  }

  Serial.print("USB_DEVICE in=");
  Serial.print(inCables);
  Serial.print(" out=");
  Serial.println(outCables);

  g_configured = true;
}

void sendDump(espmidi::AppPort &port, const uint8_t *payload, size_t length)
{
  espmidi::Message dump;
  dump.type = espmidi::MessageType::Data7;
  dump.status = 0xf0;
  dump.chunk = true;
  dump.chunkStart = true;
  dump.chunkEnd = true;
  dump.chunkData = payload;
  dump.chunkLength = length;
  expect(port.send(dump), "send dump");
}

// One message down the DIN before the test proper, to say in a single line what a
// dozen WRONG_PATH lines would otherwise have to be read for.
//
// A MIDI interface with a Thru sends back out on its DIN OUT whatever arrives on
// its DIN IN. The rig then carries every message on both paths and nothing here
// can tell the two apart, so it is not a rig this test can run on.
bool rigIsSound()
{
  g_dinArrivals = 0;
  g_usbArrivals = 0;
  g_txMicros = micros();
  toDin.sendShort(0x90, 0x3c, 0x64);
  pump(1000);
  g_txMicros = 0;

  if (g_dinArrivals > 0)
  {
    Serial.println("RIG_FAULT the interface merges DIN IN into DIN OUT (a MIDI Thru)");
    Serial.println("RIG_FAULT the two paths cannot be told apart — use an interface without one");
    return false;
  }
  if (g_usbArrivals == 0)
  {
    Serial.println("RIG_FAULT nothing reached the interface over the DIN");
    Serial.println("RIG_FAULT check the MIDI OUT circuit, its resistors, and the cable to the interface DIN IN");
    return false;
  }

  Serial.println("RIG_OK");
  return true;
}

void runTest()
{
  g_txMicros = 0;
  if (!g_configured)
  {
    Serial.println("TEST_END failed");
    Serial.println("NG");
    return;
  }

  if (!rigIsSound())
  {
    Serial.println("TEST_END failed");
    Serial.println("NG");
    return;
  }

  // Out on the DIN, back over USB. Everything in this half crosses the MIDI OUT
  // circuit, the cable and the interface's own MIDI IN.
  toDin.sendShort(0x90, 0x3c, 0x64);
  await("message from USB", 3000);
  toDin.sendShort(0xb0, 0x07, 0x40);
  await("message from USB", 3000);
  toDin.sendShort(0xe0, 0x00, 0x40);
  await("message from USB", 3000);
  toDin.sendShort(0xf8);
  await("message from USB", 3000);

  const uint8_t outward[] = {0x7d, 0x01, 0x02};
  sendDump(toDin, outward, sizeof(outward));
  await("sysex from USB", 5000);

  toDin.sendShort(0x80, 0x3c, 0x00);
  await("message from USB", 3000);

  // Out over USB, back on the DIN: the interface's MIDI OUT drives this board's
  // optocoupler.
  toUsb.sendShort(0x91, 0x40, 0x50);
  await("message from DIN", 3000);
  toUsb.sendShort(0xb1, 0x0b, 0x20);
  await("message from DIN", 3000);
  toUsb.sendShort(0xe1, 0x00, 0x40);
  await("message from DIN", 3000);
  toUsb.sendShort(0xf8);
  await("message from DIN", 3000);

  const uint8_t inward[] = {0x7d, 0x11, 0x12};
  sendDump(toUsb, inward, sizeof(inward));
  await("sysex from DIN", 5000);

  toUsb.sendShort(0x81, 0x40, 0x00);
  await("message from DIN", 3000);

  const espmidi::RouterCounters counters = router.counters();
  expect(counters.sendFailed == 0, "nothing was refused by a transport");
  expect(counters.queueFull == 0, "the queue never overflowed");

  Serial.print("COUNTERS recv=");
  Serial.print(counters.received);
  Serial.print(" deliv=");
  Serial.print(counters.delivered);
  Serial.print(" sendFailed=");
  Serial.print(counters.sendFailed);
  Serial.print(" full=");
  Serial.println(counters.queueFull);

  Serial.print("TEST_END ");
  Serial.println(g_failures == 0 ? "ok" : "failed");
  Serial.println(g_failures == 0 ? "OK" : "NG");
}

// Bring-up commands. Each one does its thing and returns, so a whole rig can be
// worked out from one console session without reflashing or unplugging anything.

// One message on the DIN, then a second of listening. Whatever arrives is printed
// by onMessage() with the path it came from, so a single line answers where the
// loop actually goes.
void sendOneToDin()
{
  Serial.println("TX_DIN 90 3C 64");
  g_txMicros = micros();
  toDin.sendShort(0x90, 0x3c, 0x64);
  pump(1000);
  Serial.println("DONE");
}

void sendOneToUsb()
{
  Serial.println("TX_USB 91 40 50");
  g_txMicros = micros();
  toUsb.sendShort(0x91, 0x40, 0x50);
  pump(1000);
  Serial.println("DONE");
}

// Listens for two seconds without sending, printing the bytes exactly as the UART
// driver hands them over. The port's parser is bypassed — nothing printed here is
// this library's interpretation of anything.
void rawListen()
{
  Serial.println("RAW_LISTEN 2 s, nothing is sent");
  const uint32_t until = millis() + 2000;
  while (static_cast<int32_t>(until - millis()) > 0)
  {
    while (Serial1.available() > 0)
    {
      Serial.print("RAW ");
      print2(static_cast<uint8_t>(Serial1.read()));
      Serial.println();
    }
  }
  Serial.println("DONE");
}

// Five seconds of traffic on the DIN, for a scope or a logic analyser. The test
// itself puts six bursts of about a millisecond across twenty seconds, which is
// hard to trigger on.
void txBurst()
{
  Serial.println("TX_BURST 5 s, a note every 200 ms");
  const uint32_t until = millis() + 5000;
  while (static_cast<int32_t>(until - millis()) > 0)
  {
    g_txMicros = micros();
    toDin.sendShort(0x90, 0x3c, 0x64);
    pump(100);
    g_txMicros = micros();
    toDin.sendShort(0x80, 0x3c, 0x00);
    pump(100);
  }
  Serial.println("DONE");
}

void printStatus()
{
  // Asked of the Arduino core rather than remembered here, so that what the
  // peripheral is actually attached to can be compared with what was requested.
  // A pin left over from an earlier begin() shows up in this line.
  Serial.print("UART1 rx=");
  Serial.print(uart_get_RxPin(1));
  Serial.print(" tx=");
  Serial.println(uart_get_TxPin(1));

  Serial.print("STATUS routes=");
  Serial.print(g_routeCount);
  Serial.print(" din=");
  Serial.print(g_dinStarted ? "started" : "not started");
  Serial.print(" usbDevices=");
  Serial.print(adapter.deviceCount());
  if (adapter.deviceCount() > 0)
  {
    Serial.print(" usbIn=");
    Serial.print(adapter.inPortCount(g_address));
    Serial.print(" usbOut=");
    Serial.print(adapter.outPortCount(g_address));
  }
  const espmidi::RouterCounters counters = router.counters();
  Serial.print(" recv=");
  Serial.print(counters.received);
  Serial.print(" deliv=");
  Serial.print(counters.delivered);
  Serial.print(" noRoute=");
  Serial.print(counters.noRoute);
  Serial.print(" sendFailed=");
  Serial.println(counters.sendFailed);
}

void printHelp()
{
  Serial.println("COMMANDS p <tx> <rx> | 1 din | 2 usb | r listen | t burst | s status | g test");
}

void setup()
{
  Serial.begin(115200);
  delay(200);
}

void handleLine()
{
  long txPin = 0;
  long rxPin = 0;

  if (parsePins(g_line, txPin, rxPin))
  {
    configure(txPin, rxPin);
    printHelp();
    return;
  }

  if (g_line[1] != '\0')
  {
    return;
  }

  // Every command is a single character and every one of them returns, so the
  // console stays usable: a rig is worked out by running them in turn rather than
  // by plugging and unplugging between runs.
  //
  // 'g' is checked as exactly one character for a second reason: a board's UART0
  // can pick up a stray byte as the flashing tool releases the line, and starting
  // the run on one would mean the console is opened after everything was said.
  switch (g_line[0])
  {
    case 'g':
      runTest();
      break;
    case '1':
      if (g_dinStarted) sendOneToDin();
      else Serial.println("NO_PINS");
      break;
    case '2':
      if (g_configured) sendOneToUsb();
      else Serial.println("NO_USB");
      break;
    case 'r':
      if (g_dinStarted) rawListen();
      else Serial.println("NO_PINS");
      break;
    case 't':
      if (g_dinStarted) txBurst();
      else Serial.println("NO_PINS");
      break;
    case 's':
      printStatus();
      break;
    case '?':
      printHelp();
      break;
    default:
      break;
  }
}

// The banner repeats rather than being printed once. The board is reset by the
// flashing tool and the console is opened after that, so anything said in the
// first moments of setup() is gone before anyone is listening.
void loop()
{
  while (Serial.available() > 0)
  {
    const char c = static_cast<char>(Serial.read());
    g_greeted = true;
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      g_line[g_lineLength] = '\0';
      const size_t length = g_lineLength;
      g_lineLength = 0;
      if (length > 0)
      {
        handleLine();
      }
      continue;
    }
    if (g_lineLength + 1 < sizeof(g_line))
    {
      g_line[g_lineLength++] = c;
    }
  }

  if (!g_greeted && static_cast<int32_t>(millis() - g_nextBanner) >= 0)
  {
    Serial.println("DIN_RIG_READY");
    g_nextBanner = millis() + 500;
  }

  pump(20);
}
