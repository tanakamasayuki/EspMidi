// The USB Device MIDI port: cables to seats, and the mount state.
//
// The port is a template over the two objects it borrows, so the whole of it runs
// on the host with stand-ins for EspUsbDeviceMidi and EspUsbDevice. What is fixed
// here is the part that has nothing to do with USB itself: which cable becomes
// which seat and in which direction, what a mount and an unmount do to the seats,
// and what happens to a cable nobody declared.
//
// The direction inversion is the thing most worth pinning down. EspUsbDevice
// names its cable counts from the host's point of view, so the host's OUT cables
// are this library's input ports. A test is the only place that keeps that from
// silently flipping.
//
// Specification: docs/PORTS.ja.md.

#include <EspMidiEspUsbDevice.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

namespace
{
int g_ran = 0;

struct FakePacket
{
  uint8_t header = 0;
  uint8_t byte1 = 0;
  uint8_t byte2 = 0;
  uint8_t byte3 = 0;
};

// Stands in for EspUsbDeviceMidi. The cable counts keep that library's names, so
// the inversion this test is about is visible right here.
struct FakeMidi
{
  uint8_t inCables = 1;  // device to host: what we send
  uint8_t outCables = 1; // host to device: what we receive
  std::deque<FakePacket> rx;
  std::vector<FakePacket> tx;
  size_t txCapacity = 0; // 0 means unlimited

  uint8_t inCableCount() const { return inCables; }
  uint8_t outCableCount() const { return outCables; }

  bool readPacket(FakePacket &packet)
  {
    if (rx.empty())
    {
      return false;
    }
    packet = rx.front();
    rx.pop_front();
    return true;
  }

  bool writePacket(const FakePacket &packet)
  {
    if (txCapacity != 0 && tx.size() >= txCapacity)
    {
      return false;
    }
    tx.push_back(packet);
    return true;
  }

  void feed(uint8_t header, uint8_t b1, uint8_t b2 = 0, uint8_t b3 = 0)
  {
    rx.push_back(FakePacket{header, b1, b2, b3});
  }
};

struct FakeDevice
{
  bool up = false;
  bool ready() const { return up; }
};

using TestPort = espmidi::BasicUsbDevicePort<FakeMidi, FakeDevice>;

struct Sink
{
  std::vector<uint8_t> statuses;
  std::vector<uint16_t> ports;
  std::vector<uint8_t> payload;
  size_t chunkEnds = 0;

  static bool write(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    self->statuses.push_back(message.status);
    self->ports.push_back(message.port.value);
    if (message.chunk)
    {
      for (size_t i = 0; i < message.chunkLength; i++)
      {
        self->payload.push_back(message.chunkData[i]);
      }
      self->chunkEnds += message.chunkEnd ? 1 : 0;
    }
    return true;
  }
};

espmidi::Message shortMessage(uint8_t *storage, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
{
  espmidi::Message message;
  espmidi::buildShortMessage(message, storage, status, d1, d2);
  return message;
}

struct Fixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  FakeMidi midi;
  FakeDevice device;
  TestPort port{router, midi, device, 0};

  // Cable counts have to be set before begin(), which is when the seats are
  // created — the same order a sketch uses, since the stack decides them.
  void start(uint8_t inCables, uint8_t outCables)
  {
    midi.inCables = inCables;
    midi.outCables = outCables;
    assert(port.begin("USB MIDI"));
  }

  void mount()
  {
    device.up = true;
    port.update();
  }

  void unmount()
  {
    device.up = false;
    port.update();
  }
};

void test_cable_counts_are_read_from_the_host_s_point_of_view()
{
  // Two cables device to host and three the other way. In this library's terms
  // that is two output ports and three input ports.
  Fixture f;
  f.start(2, 3);

  assert(f.port.outPortCount() == 2);
  assert(f.port.inPortCount() == 3);
  assert(f.registry.portCount() == 5);

  assert(f.registry.portDirection(f.port.in(0).port) == espmidi::Direction::In);
  assert(f.registry.portDirection(f.port.out(0).port) == espmidi::Direction::Out);

  // Out of range asks for a seat that was never created.
  assert(!f.port.out(2).valid());
  assert(!f.port.in(3).valid());

  espmidi::PortInfo info;
  assert(f.registry.portInfo(f.port.in(2).port, info));
  assert(info.transport == espmidi::Transport::UsbDevice);
  assert(info.index == 2);
}

void test_a_direction_with_no_cables_gets_no_ports()
{
  // Zero is not one. A device that only sends must not be given an input port
  // that the host has no way to reach.
  Fixture f;
  f.start(1, 0);

  assert(f.port.outPortCount() == 1);
  assert(f.port.inPortCount() == 0);
  assert(!f.port.in(0).valid());
  assert(f.registry.portCount() == 1);
}

void test_more_cables_than_a_packet_can_address_are_clamped()
{
  // The cable field is four bits. A stack that reported more would produce seats
  // no packet could ever refer to.
  Fixture f;
  f.start(20, 0);

  assert(f.port.outPortCount() == espmidi::MaxPortsPerEndpoint);
  assert(f.port.out(15).valid());
}

void test_the_host_decides_when_the_ports_are_usable()
{
  Fixture f;
  f.start(1, 1);

  // Before the host has configured the device there is nothing on the other end.
  assert(!f.port.available());
  assert(f.registry.portState(f.port.in(0).port) == espmidi::PortState::Disconnected);

  f.mount();
  assert(f.port.available());
  assert(f.registry.portState(f.port.in(0).port) == espmidi::PortState::Available);
  assert(f.registry.portState(f.port.out(0).port) == espmidi::PortState::Available);

  const espmidi::InPort in = f.port.in(0);
  f.unmount();
  assert(!f.port.available());
  assert(f.registry.portState(f.port.in(0).port) == espmidi::PortState::Disconnected);

  // The seat survives being unplugged, so the routes a sketch built still point
  // at it when the host comes back.
  f.mount();
  assert(f.port.in(0) == in);
  assert(f.registry.portState(f.port.in(0).port) == espmidi::PortState::Available);
}

void test_begin_is_idempotent()
{
  Fixture f;
  f.start(2, 2);
  const espmidi::InPort in = f.port.in(1);
  const size_t ports = f.registry.portCount();

  assert(f.port.begin("USB MIDI"));

  assert(f.port.in(1) == in);
  assert(f.registry.portCount() == ports);
}

void test_received_packets_reach_the_router_on_their_cable_s_port()
{
  Fixture f;
  f.start(1, 2);
  Sink sink;
  const espmidi::EndpointId observer = [&] {
    espmidi::EndpointIdentity identity;
    identity.transport = espmidi::Transport::Application;
    identity.index = 9;
    return f.registry.attachEndpoint(identity, "observer");
  }();
  const espmidi::OutPort out = f.registry.attachOutPort(observer, 0);
  f.router.setOutputSink(out, &Sink::write, &sink);
  f.router.addRoute(espmidi::InGroup::all(), out);
  f.mount();

  f.midi.feed(0x09, 0x90, 60, 100); // cable 0, note on
  f.midi.feed(0x1b, 0xb0, 7, 64);   // cable 1, control change
  f.port.update();
  f.router.update();

  assert(sink.statuses.size() == 2);
  assert(sink.statuses[0] == 0x90);
  assert(sink.ports[0] == f.port.in(0).port.value);
  assert(sink.statuses[1] == 0xb0);
  assert(sink.ports[1] == f.port.in(1).port.value);
}

void test_a_packet_on_an_undeclared_cable_is_dropped_and_counted()
{
  // The cable number is read straight from the packet header, so a host that
  // sends on a cable this device never declared would otherwise land on
  // whichever seat happened to be there.
  Fixture f;
  f.start(1, 1);
  f.mount();

  f.midi.feed(0x39, 0x90, 60, 100); // cable 3, and only cable 0 exists
  f.port.update();

  assert(f.port.unknownCablePackets() == 1);
  assert(f.router.queued() == 0);
}

void test_nothing_is_read_before_the_host_mounts()
{
  Fixture f;
  f.start(1, 1);

  f.midi.feed(0x09, 0x90, 60, 100);
  f.port.update(); // device.up is still false

  assert(f.router.queued() == 0);
  assert(f.midi.rx.size() == 1); // still there for when the host arrives
}

void test_a_read_is_bounded()
{
  Fixture f;
  f.start(1, 1);
  f.mount();

  for (size_t i = 0; i < ESPMIDI_USB_PACKETS_PER_UPDATE * 2; i++)
  {
    f.midi.feed(0x0f, 0xf8);
  }
  f.port.update();

  assert(f.midi.rx.size() == ESPMIDI_USB_PACKETS_PER_UPDATE);
}

// Sends into the port's output seats through an application port.
struct SendFixture : Fixture
{
  espmidi::AppPort app{router, "sketch"};

  void route(uint8_t cable) { router.addRoute(app.in(), port.out(cable)); }

  void send(uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
  {
    uint8_t storage[espmidi::MaxShortMessageBytes] = {};
    app.send(shortMessage(storage, status, d1, d2));
    router.update();
  }
};

void test_sending_puts_the_cable_in_the_packet_header()
{
  SendFixture f;
  f.start(2, 1);
  f.mount();
  f.route(1);

  f.send(0x90, 60, 100);

  assert(f.midi.tx.size() == 1);
  assert(f.midi.tx[0].header == 0x19); // cable 1, note on
  assert(f.midi.tx[0].byte1 == 0x90);
  assert(f.midi.tx[0].byte2 == 60);
  assert(f.midi.tx[0].byte3 == 100);
}

void test_nothing_is_sent_before_the_host_mounts()
{
  SendFixture f;
  f.start(1, 1);
  f.route(0);

  f.send(0x90, 60, 100);

  assert(f.midi.tx.empty());
  assert(f.router.counters().sendFailed == 1);
}

void test_a_full_endpoint_is_counted()
{
  SendFixture f;
  f.start(1, 1);
  f.mount();
  f.route(0);
  f.midi.txCapacity = 1;

  f.send(0x90, 60, 100);
  assert(f.router.counters().sendFailed == 0);

  f.send(0x90, 62, 100);
  assert(f.router.counters().sendFailed == 1);
}

void test_a_stream_is_split_into_packets_on_its_own_cable()
{
  SendFixture f;
  f.start(2, 1);
  f.mount();
  f.route(1);

  const uint8_t payload[] = {0x7d, 0x01, 0x02, 0x03};
  espmidi::Message dump;
  dump.type = espmidi::MessageType::Data7;
  dump.status = 0xf0;
  dump.chunk = true;
  dump.chunkStart = true;
  dump.chunkEnd = true;
  dump.chunkData = payload;
  dump.chunkLength = sizeof(payload);
  f.app.send(dump);
  f.router.update();

  // 0xF0 7D 01 02 03 0xF7 is two full groups: one SysExStart packet and one
  // SysExEnd3 packet.
  assert(f.midi.tx.size() == 2);
  assert(f.midi.tx[0].header == 0x14); // cable 1, SysExStart
  assert(f.midi.tx[0].byte1 == 0xf0);
  assert(f.midi.tx[0].byte2 == 0x7d);
  assert(f.midi.tx[0].byte3 == 0x01);
  assert(f.midi.tx[1].header == 0x17); // cable 1, SysExEnd3
  assert(f.midi.tx[1].byte1 == 0x02);
  assert(f.midi.tx[1].byte2 == 0x03);
  assert(f.midi.tx[1].byte3 == 0xf7);
}

void test_an_unmount_drops_a_stream_in_flight()
{
  // There is nobody left to send an 0xF7 to, so the stream is abandoned. What
  // must not happen is the next dump continuing the abandoned one.
  SendFixture f;
  f.start(1, 1);
  f.mount();
  f.route(0);

  const uint8_t payload[] = {0x7d};
  espmidi::Message chunk;
  chunk.type = espmidi::MessageType::Data7;
  chunk.status = 0xf0;
  chunk.chunk = true;
  chunk.chunkStart = true;
  chunk.chunkData = payload;
  chunk.chunkLength = sizeof(payload);
  f.app.send(chunk);
  f.router.update();
  const size_t sent = f.midi.tx.size();

  f.unmount();
  f.mount();

  // The continuation of the abandoned stream is refused rather than encoded.
  chunk.chunkStart = false;
  chunk.chunkEnd = true;
  f.app.send(chunk);
  f.router.update();

  assert(f.midi.tx.size() == sent);
  assert(f.router.counters().sendFailed >= 1);
}

void test_a_received_stream_arrives_as_chunks()
{
  Fixture f;
  f.start(1, 1);
  Sink sink;
  espmidi::AppPort app{f.router, "sketch"};
  app.onMessage([](void *context, const espmidi::Message &message) { Sink::write(context, message); }, &sink);
  f.router.addRoute(f.port.in(0), app.out());
  f.mount();

  f.midi.feed(0x04, 0xf0, 0x7d, 0x01); // SysExStart
  f.midi.feed(0x06, 0x02, 0xf7);       // SysExEnd2
  f.port.update();
  f.router.update();

  // The framing bytes are the wire's; what a route sees is the payload.
  assert(sink.payload.size() == 3);
  assert(sink.payload[0] == 0x7d);
  assert(sink.payload[1] == 0x01);
  assert(sink.payload[2] == 0x02);
  assert(sink.chunkEnds == 1);
}

void test_a_round_trip_between_two_cables()
{
  // Cable 1 out, cable 0 in, with the packets handed back as if the host had
  // echoed them. The shape the peer test runs on hardware.
  SendFixture f;
  f.start(2, 2);
  f.mount();
  f.route(1);

  Sink sink;
  const espmidi::EndpointId observer = [&] {
    espmidi::EndpointIdentity identity;
    identity.transport = espmidi::Transport::Application;
    identity.index = 9;
    return f.registry.attachEndpoint(identity, "observer");
  }();
  const espmidi::OutPort out = f.registry.attachOutPort(observer, 0);
  f.router.setOutputSink(out, &Sink::write, &sink);
  f.router.addRoute(f.port.in(0), out);

  f.send(0xb0, 7, 64);
  assert(f.midi.tx.size() == 1);

  // The host echoes it on cable 0.
  const FakePacket echoed = f.midi.tx[0];
  f.midi.feed(static_cast<uint8_t>(echoed.header & 0x0f), echoed.byte1, echoed.byte2, echoed.byte3);
  f.port.update();
  f.router.update();

  assert(sink.statuses.size() == 1);
  assert(sink.statuses[0] == 0xb0);
  assert(sink.ports[0] == f.port.in(0).port.value);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_cable_counts_are_read_from_the_host_s_point_of_view);
  run(test_a_direction_with_no_cables_gets_no_ports);
  run(test_more_cables_than_a_packet_can_address_are_clamped);
  run(test_the_host_decides_when_the_ports_are_usable);
  run(test_begin_is_idempotent);
  run(test_received_packets_reach_the_router_on_their_cable_s_port);
  run(test_a_packet_on_an_undeclared_cable_is_dropped_and_counted);
  run(test_nothing_is_read_before_the_host_mounts);
  run(test_a_read_is_bounded);
  run(test_sending_puts_the_cable_in_the_packet_header);
  run(test_nothing_is_sent_before_the_host_mounts);
  run(test_a_full_endpoint_is_counted);
  run(test_a_stream_is_split_into_packets_on_its_own_cable);
  run(test_an_unmount_drops_a_stream_in_flight);
  run(test_a_received_stream_arrives_as_chunks);
  run(test_a_round_trip_between_two_cables);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
