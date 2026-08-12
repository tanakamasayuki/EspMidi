// USB MIDI between two EspMidi ports on one board.
//
// An ESP32-P4 has two USB peripherals, so one board can be a USB MIDI device and
// a USB MIDI host at the same time. With its two connectors joined, the board's
// own USB Device port talks to its own USB Host port over a real USB cable — and
// both ends are EspMidi, in one router.
//
//   espmidi::UsbDevicePort  ──→ USB FS ──→ espmidi::UsbHostPort
//   espmidi::UsbHostPort    ──→ USB FS ──→ espmidi::UsbDevicePort
//
// This is the test the loopback directory exists for: EspMidi treats several
// ports as one system, and here the whole system is on one board.
//
// **The two ports invert their cable counts in opposite directions**, and here
// that has to cancel exactly. The device declares 2 cables device-to-host and 3
// host-to-device; the host port must therefore see 2 inputs and 3 outputs while
// the device port sees 3 inputs and 2 outputs. Both readings are printed and
// checked against each other.
//
// **Nothing routes between the two USB ports directly.** They are two ends of one
// cable, so a route from either to the other would send every message round the
// loop forever — the same-endpoint rule cannot see that, because they genuinely
// are different endpoints. An application port sits in the middle instead.
//
// The console is UART0, the board's external USB-serial chip, so neither USB
// peripheral is needed for it.

#include <EspMidiEspUsbDevice.h>
#include <EspMidiEspUsbHost.h>

// Host-view, as EspUsbDevice names them: IN is device to host, OUT is host to
// device. Asymmetric so a swapped direction cannot hide.
static constexpr uint8_t IN_CABLES = 2;
static constexpr uint8_t OUT_CABLES = 3;

EspUsbHost usbHost;
EspUsbDevice usbDevice;
EspUsbDeviceMidi usbDeviceMidi(usbDevice, IN_CABLES, OUT_CABLES);

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UsbHostPort hostPort(router, usbHost);
espmidi::UsbDevicePort devicePort(router, usbDeviceMidi, usbDevice);
espmidi::AppPort app(router, "test");

int g_failures = 0;
uint8_t g_sysex[16] = {};
size_t g_sysexLength = 0;
bool g_greeted = false;
uint32_t g_nextBanner = 0;

// One route per destination, all disabled until used, so a message goes exactly
// where a step wants it and nowhere else.
espmidi::Route g_toDevice[espmidi::MaxPortsPerEndpoint];
espmidi::Route g_toHost[espmidi::MaxPortsPerEndpoint];

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
    usbDevice.task();
    devicePort.update();
    hostPort.update();
    router.update();
  }
}

// Where a message came from: which of the two ports, and which cable.
void printSource(espmidi::PortId port)
{
  for (uint8_t cable = 0; cable < devicePort.inPortCount(); cable++)
  {
    if (devicePort.in(cable).port == port)
    {
      Serial.print(" from=device port=");
      Serial.print(cable);
      return;
    }
  }
  for (size_t i = 0; i < hostPort.deviceCount(); i++)
  {
    const uint8_t address = hostPort.addressAt(i);
    for (uint8_t cable = 0; cable < hostPort.inPortCount(address); cable++)
    {
      if (hostPort.in(address, cable).port == port)
      {
        Serial.print(" from=host port=");
        Serial.print(cable);
        return;
      }
    }
  }
  Serial.print(" from=? port=?");
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

void enableOnly(espmidi::Route route)
{
  for (uint8_t i = 0; i < espmidi::MaxPortsPerEndpoint; i++)
  {
    router.setRouteEnabled(g_toDevice[i], g_toDevice[i] == route);
    router.setRouteEnabled(g_toHost[i], g_toHost[i] == route);
  }
}

void send(espmidi::Route route, const espmidi::Message &message)
{
  enableOnly(route);
  expect(app.send(message), "queued");
  pump(50);
}

void sendShort(espmidi::Route route, uint8_t status, uint8_t d1, uint8_t d2)
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message message;
  espmidi::buildShortMessage(message, bytes, status, d1, d2);
  send(route, message);
}

void sendSysEx(espmidi::Route route, uint8_t marker)
{
  const uint8_t payload[] = {0x7d, marker, static_cast<uint8_t>(marker + 1)};
  espmidi::Message dump;
  dump.type = espmidi::MessageType::Data7;
  dump.status = 0xf0;
  dump.chunk = true;
  dump.chunkStart = true;
  dump.chunkEnd = true;
  dump.chunkData = payload;
  dump.chunkLength = sizeof(payload);
  send(route, dump);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // 1) The stacks. The host takes the full-speed port so the device peripheral is
  //    free for the other end of the cable.
  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_FULL_SPEED;
  expect(usbHost.begin(hostConfig), "host stack");

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4017;
  deviceConfig.manufacturer = "EspMidi";
  deviceConfig.product = "EspMidi Loopback";
  expect(usbDevice.begin(deviceConfig), "device stack");

  // 2) The ports.
  expect(devicePort.begin("USB device"), "device port");
  expect(hostPort.begin(), "host port");

  // 3) The routes: an application port in the middle, never between the two USB
  //    ports directly.
  router.addRoute(espmidi::InGroup::all(), app.out());
  app.onMessage(onMessage);
}

void runTest()
{
  // The host has to enumerate the device before either side has all its seats.
  const uint32_t until = millis() + 10000;
  while (hostPort.deviceCount() == 0 && static_cast<int32_t>(until - millis()) > 0)
  {
    pump(50);
  }
  expect(hostPort.deviceCount() == 1, "the host found the device");
  const uint8_t address = hostPort.addressAt(0);

  Serial.print("DEVICE_PORTS in=");
  Serial.print(devicePort.inPortCount());
  Serial.print(" out=");
  Serial.println(devicePort.outPortCount());
  Serial.print("HOST_PORTS in=");
  Serial.print(hostPort.inPortCount(address));
  Serial.print(" out=");
  Serial.println(hostPort.outPortCount(address));

  // The two inversions have to cancel: they are two ends of one cable.
  expect(devicePort.outPortCount() == hostPort.inPortCount(address), "device out matches host in");
  expect(devicePort.inPortCount() == hostPort.outPortCount(address), "device in matches host out");
  expect(devicePort.outPortCount() == IN_CABLES, "declared device-to-host cables");
  expect(devicePort.inPortCount() == OUT_CABLES, "declared host-to-device cables");

  for (uint8_t cable = 0; cable < devicePort.outPortCount(); cable++)
  {
    g_toHost[cable] = router.addRoute(app.in(), devicePort.out(cable));
    router.setRouteEnabled(g_toHost[cable], false);
  }
  for (uint8_t cable = 0; cable < hostPort.outPortCount(address); cable++)
  {
    g_toDevice[cable] = router.addRoute(app.in(), hostPort.out(address, cable));
    router.setRouteEnabled(g_toDevice[cable], false);
  }

  // Device to host, on the last cable that direction has.
  sendShort(g_toHost[1], 0x90, 0x3c, 0x64);
  sendShort(g_toHost[0], 0xb0, 0x07, 0x40);

  // Host to device, on a cable that exists only in this direction.
  sendShort(g_toDevice[2], 0x90, 0x40, 0x50);
  sendShort(g_toDevice[0], 0xb0, 0x0b, 0x20);

  // A dump each way.
  sendSysEx(g_toHost[1], 0x11);
  sendSysEx(g_toDevice[2], 0x21);

  const espmidi::RouterCounters counters = router.counters();
  Serial.print("COUNTERS failed=");
  Serial.print(counters.sendFailed);
  Serial.print(" full=");
  Serial.print(counters.queueFull);
  Serial.print(" unknownDevice=");
  Serial.print(devicePort.unknownCablePackets());
  Serial.print(" unknownHost=");
  Serial.print(hostPort.unknownCablePackets());
  Serial.print(" dropped=");
  Serial.println(hostPort.droppedPackets());

  expect(counters.sendFailed == 0, "nothing was refused");
  expect(counters.queueFull == 0, "the queue never overflowed");
  expect(devicePort.unknownCablePackets() == 0, "no unknown cable at the device");
  expect(hostPort.unknownCablePackets() == 0, "no unknown cable at the host");
  expect(hostPort.droppedPackets() == 0, "nothing was dropped");

  Serial.print("TEST_END ");
  Serial.println(g_failures == 0 ? "ok" : "failed");
  Serial.println(g_failures == 0 ? "OK" : "NG");
}

// The banner repeats and the run waits to be asked: the board is reset by the
// flashing tool and the console is opened after that (tests/TEST_PLAN.ja.md).
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
