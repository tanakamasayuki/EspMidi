// The UART port: a HardwareSerial on one side, the router on the other.
//
// The port is a template over the serial object precisely so this can run on the
// host. What is fixed here is everything except the wire itself — the seats it
// supplies, the parsing of what arrives, the framing of what leaves, what a full
// transmit buffer does, and what happens to an interrupted dump when the port is
// closed. The loopback test on hardware then proves only the part that no host
// test can: that the bytes reach the pin.
//
// Specification: docs/PORTS.ja.md.

#include <EspMidiUart.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

namespace
{
int g_ran = 0;

// Enough of HardwareSerial for the port, plus the two things a real one does
// that matter here: it can be closed, and it can refuse bytes when its transmit
// buffer is full.
struct FakeSerial
{
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  bool open = false;
  unsigned long baud = 0;
  int8_t rxPin = 0;
  int8_t txPin = 0;
  size_t txCapacity = 0; // 0 means unlimited

  void begin(unsigned long baudRate, uint32_t /*config*/, int8_t rx_, int8_t tx_)
  {
    open = true;
    baud = baudRate;
    rxPin = rx_;
    txPin = tx_;
  }

  void end()
  {
    open = false;
    ends++;
  }

  int ends = 0;

  int available() const { return open ? static_cast<int>(rx.size()) : 0; }

  int read()
  {
    if (!open || rx.empty())
    {
      return -1;
    }
    const uint8_t byte = rx.front();
    rx.pop_front();
    return byte;
  }

  size_t write(const uint8_t *data, size_t length)
  {
    if (!open)
    {
      return 0;
    }
    if (txCapacity != 0 && tx.size() + length > txCapacity)
    {
      return 0;
    }
    tx.insert(tx.end(), data, data + length);
    return length;
  }

  void feed(std::initializer_list<uint8_t> bytes) { rx.insert(rx.end(), bytes.begin(), bytes.end()); }

  bool sent(std::initializer_list<uint8_t> expected) const
  {
    return tx.size() == expected.size() && std::memcmp(tx.data(), expected.begin(), tx.size()) == 0;
  }
};

using TestUartPort = espmidi::BasicUartPort<FakeSerial>;

// What a route out of the UART's input port sees.
struct Sink
{
  std::vector<uint8_t> statuses;
  std::vector<uint8_t> payload;
  size_t chunkStarts = 0;
  size_t chunkEnds = 0;

  static bool write(void *context, const espmidi::Message &message)
  {
    record(context, message);
    return true;
  }

  static void record(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    self->statuses.push_back(message.status);
    if (message.chunk)
    {
      self->chunkStarts += message.chunkStart ? 1 : 0;
      self->chunkEnds += message.chunkEnd ? 1 : 0;
      for (size_t i = 0; i < message.chunkLength; i++)
      {
        self->payload.push_back(message.chunkData[i]);
      }
    }
  }
};

espmidi::Message shortMessage(uint8_t *storage, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
{
  espmidi::Message message;
  espmidi::buildShortMessage(message, storage, status, d1, d2);
  return message;
}

// A UART port, an application port to inject into it, and a sink to observe what
// it receives.
struct Fixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  FakeSerial serial;
  TestUartPort uart{router, serial, 1};
  espmidi::AppPort app{router, "sketch"};
  Sink sink;

  Fixture()
  {
    assert(uart.begin("UART MIDI", 20, 19));
    router.addRoute(app.in(), uart.out());
  }

  // Puts a message on the UART's output port the way a route would.
  void send(uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
  {
    uint8_t storage[espmidi::MaxShortMessageBytes] = {};
    app.send(shortMessage(storage, status, d1, d2));
    router.update();
  }

  // Routes what the UART receives into `sink`.
  void observe()
  {
    const espmidi::EndpointId observer = registry.attachEndpoint(applicationIdentity(), "observer");
    const espmidi::OutPort out = registry.attachOutPort(observer, 0);
    router.setOutputSink(out, &Sink::write, &sink);
    router.addRoute(uart.in(), out);
  }

  static espmidi::EndpointIdentity applicationIdentity()
  {
    espmidi::EndpointIdentity identity;
    identity.transport = espmidi::Transport::Application;
    identity.index = 9;
    return identity;
  }
};

void test_begin_supplies_one_endpoint_with_two_ports()
{
  Fixture f;

  assert(f.uart.endpoint().valid());
  assert(f.uart.in().valid());
  assert(f.uart.out().valid());
  assert(f.uart.started());

  espmidi::PortInfo info;
  assert(f.registry.portInfo(f.uart.in().port, info));
  assert(info.transport == espmidi::Transport::Uart);
  assert(info.direction == espmidi::Direction::In);
  assert(info.state == espmidi::PortState::Available);
  assert(std::strcmp(info.name, "UART MIDI") == 0);

  // 31250 baud is the one rate MIDI 1.0 has, and the pins are passed straight
  // through so a sketch can put the port anywhere the matrix reaches.
  assert(f.serial.open);
  assert(f.serial.baud == 31250);
  assert(f.serial.rxPin == 20 && f.serial.txPin == 19);
}

void test_begin_is_idempotent()
{
  // Reconfiguring is calling begin() again, and the seats have to survive it, or
  // every route the sketch built would have to be rebuilt with them.
  Fixture f;
  const espmidi::InPort in = f.uart.in();
  const espmidi::OutPort out = f.uart.out();
  const size_t ports = f.registry.portCount();

  assert(f.uart.begin("UART MIDI", 20, 19));

  assert(f.uart.in() == in);
  assert(f.uart.out() == out);
  assert(f.registry.portCount() == ports);
}

void test_reopening_releases_the_previous_pins()
{
  // On an ESP32 a peripheral reaches a pad through the GPIO matrix, and opening a
  // second pad does not take the first one back. So begin() has to close what it
  // opened before, or a board reconfigured onto new pins keeps transmitting on the
  // old ones — which is exactly what happened on the bench.
  Fixture f;
  assert(f.serial.ends == 0);

  assert(f.uart.begin("UART MIDI", 7, 6));

  assert(f.serial.ends == 1);
  assert(f.serial.open);
  assert(f.serial.rxPin == 7);
  assert(f.serial.txPin == 6);
}

void test_received_bytes_reach_the_router()
{
  Fixture f;
  f.observe();

  f.serial.feed({0x90, 60, 100, 62, 100, 0xf8});
  f.uart.update();
  f.router.update();

  // Running status is resolved by the core parser, so a route sees three
  // complete messages.
  assert(f.sink.statuses.size() == 3);
  assert(f.sink.statuses[0] == 0x90);
  assert(f.sink.statuses[1] == 0x90);
  assert(f.sink.statuses[2] == 0xf8);
}

void test_nothing_is_received_before_begin()
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  FakeSerial serial;
  TestUartPort uart{router, serial, 1};

  serial.feed({0x90, 60, 100});
  uart.update(); // must not touch a serial port that was never opened

  assert(!uart.started());
  assert(router.queued() == 0);
}

void test_a_read_is_bounded()
{
  // A device streaming a dump must not be able to hold loop(). What is not read
  // now is still in the driver's buffer for the next pass.
  Fixture f;
  f.observe();

  for (int i = 0; i < ESPMIDI_UART_RX_BYTES * 2; i++)
  {
    f.serial.rx.push_back(0xf8); // clock, one byte per message
  }
  f.uart.update();

  assert(f.serial.available() == ESPMIDI_UART_RX_BYTES);
}

void test_sending_writes_the_wire_bytes()
{
  Fixture f;

  f.send(0x90, 60, 100);
  f.send(0xc0, 5);

  assert(f.serial.sent({0x90, 60, 100, 0xc0, 5}));
}

void test_sending_a_stream_adds_its_framing()
{
  Fixture f;
  const uint8_t payload[] = {0x7d, 0x01};

  espmidi::Message chunk;
  chunk.type = espmidi::MessageType::Data7;
  chunk.status = 0xf0;
  chunk.chunk = true;
  chunk.chunkStart = true;
  chunk.chunkData = payload;
  chunk.chunkLength = sizeof(payload);
  f.app.send(chunk);

  chunk.chunkStart = false;
  chunk.chunkEnd = true;
  f.app.send(chunk);
  f.router.update();

  assert(f.serial.sent({0xf0, 0x7d, 0x01, 0x7d, 0x01, 0xf7}));
}

void test_a_full_transmit_buffer_is_counted()
{
  // The transport refusing bytes is not a routing failure and must not be
  // silent: it is the counter a sketch watches when a device stops responding.
  Fixture f;
  f.serial.txCapacity = 3;

  f.send(0x90, 60, 100);
  assert(f.router.counters().sendFailed == 0);

  f.send(0x90, 62, 100);
  assert(f.router.counters().sendFailed == 1);
  assert(f.serial.tx.size() == 3);
}

void test_end_closes_an_interrupted_stream()
{
  // The device on the other end is holding a partial dump. Without the
  // terminator it waits for the rest of a message that is never coming.
  Fixture f;
  const uint8_t payload[] = {0x41};

  espmidi::Message chunk;
  chunk.type = espmidi::MessageType::Data7;
  chunk.status = 0xf0;
  chunk.chunk = true;
  chunk.chunkStart = true;
  chunk.chunkData = payload;
  chunk.chunkLength = sizeof(payload);
  f.app.send(chunk);
  f.router.update();

  f.uart.end();

  assert(f.serial.sent({0xf0, 0x41, 0xf7}));
  assert(!f.serial.open);
  assert(!f.uart.started());

  // The seat stays. It is the connection that went away, not the port.
  assert(f.uart.in().valid());
  assert(f.registry.portState(f.uart.in().port) == espmidi::PortState::Disconnected);
}

void test_two_uart_ports_route_to_each_other()
{
  // The shape the loopback test runs on hardware, with the wire replaced by a
  // copy between the two fake serials.
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  FakeSerial serialA;
  FakeSerial serialB;
  TestUartPort a{router, serialA, 1};
  TestUartPort b{router, serialB, 2};
  assert(a.begin("A"));
  assert(b.begin("B"));

  Sink sink;
  espmidi::AppPort app{router, "sketch"};
  app.onMessage(&Sink::record, &sink);
  router.addRoute(app.in(), a.out());
  router.addRoute(b.in(), app.out());

  uint8_t storage[espmidi::MaxShortMessageBytes] = {};
  app.send(shortMessage(storage, 0x90, 60, 100));
  router.update();

  // The wire.
  for (uint8_t byte : serialA.tx)
  {
    serialB.rx.push_back(byte);
  }

  b.update();
  router.update();

  assert(sink.statuses.size() == 1);
  assert(sink.statuses[0] == 0x90);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_begin_supplies_one_endpoint_with_two_ports);
  run(test_begin_is_idempotent);
  run(test_reopening_releases_the_previous_pins);
  run(test_received_bytes_reach_the_router);
  run(test_nothing_is_received_before_begin);
  run(test_a_read_is_bounded);
  run(test_sending_writes_the_wire_bytes);
  run(test_sending_a_stream_adds_its_framing);
  run(test_a_full_transmit_buffer_is_counted);
  run(test_end_closes_an_interrupted_stream);
  run(test_two_uart_ports_route_to_each_other);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
