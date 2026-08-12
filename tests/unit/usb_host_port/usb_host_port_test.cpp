// The USB Host MIDI port: devices that come and go, and the seats they get.
//
// This is the first port whose endpoints are discovered rather than declared, so
// what is fixed here is mostly about time: what happens when a device appears
// before its interface is claimed, when it goes away mid-dump, when it comes back,
// and when it comes back without a serial number to be recognised by.
//
// The stand-in for EspUsbHost is a plain struct. The port deduces the two
// structures it needs from the transport's own signatures, so nothing here has to
// agree with the real library beyond the shape of four methods.
//
// Specification: docs/PORTS.ja.md.

#include <EspMidiEspUsbHost.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
int g_ran = 0;

struct FakeMidiMessage
{
  uint8_t address = 0;
  const uint8_t *raw = nullptr;
  size_t length = 0;
};

struct FakeDeviceInfo
{
  uint8_t address = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char *product = "";
  const char *serial = "";
  bool isHub = false;
};

struct FakePortInfo
{
  uint8_t inCableCount = 0;
  uint8_t outCableCount = 0;
};

// Everything the port asks of EspUsbHost, and nothing else.
struct FakeHost
{
  struct Attached
  {
    FakeDeviceInfo info;
    FakePortInfo ports;
    bool midiReady = true; // false while the interface has not been claimed
  };

  std::vector<Attached> attached;
  std::function<void(const FakeMidiMessage &)> midiCallback;
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>> sent;
  bool sendOk = true;

  void onMidiMessage(std::function<void(const FakeMidiMessage &)> callback) { midiCallback = std::move(callback); }

  size_t getDevices(FakeDeviceInfo *devices, size_t maxDevices) const
  {
    size_t count = 0;
    for (const Attached &device : attached)
    {
      if (count >= maxDevices)
      {
        break;
      }
      devices[count++] = device.info;
    }
    return count;
  }

  bool getMidiPortInfo(FakePortInfo &info, uint8_t address) const
  {
    for (const Attached &device : attached)
    {
      if (device.info.address == address && device.midiReady)
      {
        info = device.ports;
        return true;
      }
    }
    return false;
  }

  bool midiSend(const uint8_t *data, size_t length, uint8_t address)
  {
    if (!sendOk)
    {
      return false;
    }
    sent.emplace_back(address, std::vector<uint8_t>(data, data + length));
    return true;
  }

  // --- Driving the fake ---------------------------------------------------

  void plug(uint8_t address, uint8_t inCables, uint8_t outCables, const char *serial = "", bool midiReady = true)
  {
    Attached device;
    device.info.address = address;
    device.info.vid = 0x1234;
    device.info.pid = 0x5678;
    device.info.product = "Fake Keyboard";
    device.info.serial = serial;
    device.ports.inCableCount = inCables;
    device.ports.outCableCount = outCables;
    device.midiReady = midiReady;
    attached.push_back(device);
  }

  void unplug(uint8_t address)
  {
    for (size_t i = 0; i < attached.size(); i++)
    {
      if (attached[i].info.address == address)
      {
        attached.erase(attached.begin() + static_cast<long>(i));
        return;
      }
    }
  }

  void claim(uint8_t address)
  {
    for (Attached &device : attached)
    {
      if (device.info.address == address)
      {
        device.midiReady = true;
      }
    }
  }

  // Delivers one packet the way the library's own callback would.
  void deliver(uint8_t address, uint8_t header, uint8_t b1, uint8_t b2 = 0, uint8_t b3 = 0)
  {
    const uint8_t packet[] = {header, b1, b2, b3};
    FakeMidiMessage message;
    message.address = address;
    message.raw = packet;
    message.length = sizeof(packet);
    if (midiCallback)
    {
      midiCallback(message);
    }
  }
};

using TestPort = espmidi::BasicUsbHostPort<FakeHost>;

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

struct Fixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  FakeHost host;
  TestPort port{router, host};
  Sink sink;
  espmidi::OutPort observer;
  uint32_t now = 0;

  Fixture()
  {
    assert(port.begin());

    espmidi::EndpointIdentity identity;
    identity.transport = espmidi::Transport::Application;
    identity.index = 9;
    const espmidi::EndpointId endpoint = registry.attachEndpoint(identity, "observer");
    observer = registry.attachOutPort(endpoint, 0);
    router.setOutputSink(observer, &Sink::write, &sink);
    // Whatever appears takes part, without the sketch naming it first.
    router.addRoute(espmidi::InGroup::all(), observer);
  }

  // A pass of loop(), far enough ahead that the port polls again.
  void tick()
  {
    now += ESPMIDI_USB_HOST_POLL_MS + 1;
    port.update(now);
    router.update();
  }
};

void test_a_device_gets_one_endpoint_and_a_port_per_cable()
{
  Fixture f;
  f.host.plug(3, 2, 1);
  f.tick();

  assert(f.port.deviceCount() == 1);
  assert(f.port.endpointFor(3).valid());

  // Not inverted: inCableCount is device to host, which is what we receive.
  assert(f.port.inPortCount(3) == 2);
  assert(f.port.outPortCount(3) == 1);
  assert(f.registry.portDirection(f.port.in(3, 1).port) == espmidi::Direction::In);
  assert(f.registry.portDirection(f.port.out(3, 0).port) == espmidi::Direction::Out);
  assert(!f.port.out(3, 1).valid());

  espmidi::PortInfo info;
  assert(f.registry.portInfo(f.port.in(3, 0).port, info));
  assert(info.transport == espmidi::Transport::UsbHost);
  assert(info.state == espmidi::PortState::Available);
  assert(std::strcmp(info.name, "Fake Keyboard") == 0);
}

void test_a_device_whose_interface_is_not_claimed_yet_is_looked_at_again()
{
  // A device enumerates before its MIDI interface is claimed, so the cable counts
  // are not available on the first poll. Giving up there would leave a keyboard
  // that took a moment to settle with no seats at all.
  Fixture f;
  f.host.plug(3, 1, 1, "", false);
  f.tick();
  assert(f.port.deviceCount() == 0);

  f.host.claim(3);
  f.tick();
  assert(f.port.deviceCount() == 1);
  assert(f.port.in(3).valid());
}

void test_a_device_with_no_midi_is_ignored()
{
  Fixture f;
  f.host.plug(3, 1, 1, "", false); // never becomes ready
  f.host.attached[0].info.product = "Some Mouse";
  f.tick();
  f.tick();

  assert(f.port.deviceCount() == 0);
  assert(f.registry.portCount() == 1); // only the observer
}

void test_a_hub_is_not_a_midi_device()
{
  Fixture f;
  f.host.plug(2, 1, 1);
  f.host.attached[0].info.isHub = true;
  f.tick();

  assert(f.port.deviceCount() == 0);
}

void test_received_packets_arrive_on_their_cable_s_port()
{
  Fixture f;
  f.host.plug(3, 2, 1);
  f.tick();

  f.host.deliver(3, 0x09, 0x90, 60, 100); // cable 0
  f.host.deliver(3, 0x1b, 0xb0, 7, 64);   // cable 1
  f.tick();

  assert(f.sink.statuses.size() == 2);
  assert(f.sink.ports[0] == f.port.in(3, 0).port.value);
  assert(f.sink.statuses[1] == 0xb0);
  assert(f.sink.ports[1] == f.port.in(3, 1).port.value);
}

void test_a_packet_from_an_unknown_device_is_discarded()
{
  // The callback runs on the transport's task and cannot ask whether a seat
  // exists, so a packet can outlive the device it came from by one pass.
  Fixture f;
  f.host.plug(3, 1, 1);
  f.tick();

  f.host.deliver(9, 0x09, 0x90, 60, 100); // no seat for address 9
  f.tick();

  assert(f.sink.statuses.empty());
}

void test_a_packet_on_an_undeclared_cable_is_counted()
{
  Fixture f;
  f.host.plug(3, 1, 1);
  f.tick();

  f.host.deliver(3, 0x59, 0x90, 60, 100); // cable 5, and only cable 0 exists
  f.tick();

  assert(f.port.unknownCablePackets() == 1);
  assert(f.sink.statuses.empty());
}

void test_a_stream_is_concatenated_across_packets()
{
  // EspUsbHost hands over one packet at a time and does not join a dump back
  // together, so this is the core decoder's work.
  Fixture f;
  f.host.plug(3, 1, 1);
  f.tick();

  f.host.deliver(3, 0x04, 0xf0, 0x7d, 0x01); // SysEx starts
  f.host.deliver(3, 0x04, 0x02, 0x03, 0x04); // continues
  f.host.deliver(3, 0x05, 0xf7);             // ends with one byte
  f.tick();

  assert(f.sink.payload.size() == 5);
  assert(f.sink.payload[0] == 0x7d);
  assert(f.sink.payload[4] == 0x04);
  assert(f.sink.chunkEnds == 1);
}

void test_a_disconnect_keeps_the_seat_and_stops_the_sending()
{
  Fixture f;
  f.host.plug(3, 1, 1, "SN-1");
  f.tick();
  const espmidi::InPort in = f.port.in(3);
  const espmidi::OutPort out = f.port.out(3);

  espmidi::AppPort app{f.router, "sketch"};
  f.router.addRoute(app.in(), out);
  app.sendShort(0x90, 60, 100);
  f.router.update();
  assert(f.host.sent.size() == 1);

  f.host.unplug(3);
  f.tick();

  assert(f.port.deviceCount() == 0);
  assert(f.registry.portState(in.port) == espmidi::PortState::Disconnected);

  // The route still points at the seat, and a send through it has to fail rather
  // than reach whatever device takes address 3 next.
  app.sendShort(0x90, 62, 100);
  f.router.update();
  assert(f.host.sent.size() == 1);
  assert(f.router.counters().sendFailed >= 1);
}

void test_a_device_with_a_serial_comes_back_to_the_same_seat()
{
  Fixture f;
  f.host.plug(3, 1, 1, "SN-1");
  f.tick();
  const espmidi::InPort in = f.port.in(3);
  const espmidi::EndpointId endpoint = f.port.endpointFor(3);

  f.host.unplug(3);
  f.tick();

  // Back on a different address, which is whatever the stack handed out this
  // time. The seat is matched on the identity, not the address.
  f.host.plug(7, 1, 1, "SN-1");
  f.tick();

  assert(f.port.endpointFor(7) == endpoint);
  assert(f.port.in(7) == in);
  assert(f.registry.portState(in.port) == espmidi::PortState::Available);
  assert(f.registry.endpointCount() == 2); // the device and the observer
}

void test_a_device_with_no_serial_gets_a_fresh_seat()
{
  // Guessing would hand the previous device's routing to a different one. A
  // keyboard with no serial number is a different keyboard every time.
  Fixture f;
  f.host.plug(3, 1, 1);
  f.tick();
  const espmidi::InPort first = f.port.in(3);

  f.host.unplug(3);
  f.tick();
  f.host.plug(3, 1, 1);
  f.tick();

  assert(f.port.in(3) != first);
  assert(f.registry.endpointCount() == 3);
}

void test_several_devices_at_once()
{
  Fixture f;
  f.host.plug(3, 1, 1, "SN-A");
  f.host.plug(4, 2, 2, "SN-B");
  f.tick();

  assert(f.port.deviceCount() == 2);
  assert(f.port.inPortCount(3) == 1);
  assert(f.port.inPortCount(4) == 2);
  assert(f.port.in(3) != f.port.in(4, 0));

  f.host.deliver(3, 0x09, 0x90, 60, 100);
  f.host.deliver(4, 0x19, 0x90, 62, 100);
  f.tick();

  assert(f.sink.ports.size() == 2);
  assert(f.sink.ports[0] == f.port.in(3).port.value);
  assert(f.sink.ports[1] == f.port.in(4, 1).port.value);
}

void test_more_devices_than_there_is_room_for_are_counted()
{
  Fixture f;
  for (uint8_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES + 2; i++)
  {
    f.host.plug(static_cast<uint8_t>(10 + i), 1, 1, ("SN-" + std::to_string(i)).c_str());
  }
  f.tick();

  // getDevices() is asked for at most as many as there is room for, so the ones
  // past the end are not even seen; either way nothing is silently wrong.
  assert(f.port.deviceCount() <= ESPMIDI_MAX_USB_HOST_DEVICES);
}

void test_sending_puts_the_cable_in_the_header_and_addresses_the_device()
{
  Fixture f;
  f.host.plug(3, 1, 2, "SN-1");
  f.tick();

  espmidi::AppPort app{f.router, "sketch"};
  f.router.addRoute(app.in(), f.port.out(3, 1));
  app.sendShort(0xb0, 7, 64);
  f.router.update();

  assert(f.host.sent.size() == 1);
  assert(f.host.sent[0].first == 3);
  const std::vector<uint8_t> &packet = f.host.sent[0].second;
  assert(packet.size() == 4);
  assert(packet[0] == 0x1b); // cable 1, control change
  assert(packet[1] == 0xb0);
  assert(packet[2] == 7);
  assert(packet[3] == 64);
}

void test_a_stream_leaves_in_one_transfer()
{
  // Splitting a dump across transfers would let another cable's packets land in
  // the middle of it.
  Fixture f;
  f.host.plug(3, 1, 1, "SN-1");
  f.tick();

  espmidi::AppPort app{f.router, "sketch"};
  f.router.addRoute(app.in(), f.port.out(3));

  const uint8_t payload[] = {0x7d, 0x01, 0x02, 0x03};
  espmidi::Message dump;
  dump.type = espmidi::MessageType::Data7;
  dump.status = 0xf0;
  dump.chunk = true;
  dump.chunkStart = true;
  dump.chunkEnd = true;
  dump.chunkData = payload;
  dump.chunkLength = sizeof(payload);
  app.send(dump);
  f.router.update();

  assert(f.host.sent.size() == 1);
  assert(f.host.sent[0].second.size() == 8); // two packets, one transfer
  assert(f.host.sent[0].second[0] == 0x04);  // SysExStart
  assert(f.host.sent[0].second[4] == 0x07);  // SysExEnd3
}

void test_a_refused_transfer_is_counted()
{
  Fixture f;
  f.host.plug(3, 1, 1, "SN-1");
  f.tick();
  f.host.sendOk = false;

  espmidi::AppPort app{f.router, "sketch"};
  f.router.addRoute(app.in(), f.port.out(3));
  app.sendShort(0x90, 60, 100);
  f.router.update();

  assert(f.router.counters().sendFailed == 1);
}

void test_the_ring_bounds_what_one_pass_can_leave_behind()
{
  // The transport's task can produce faster than loop() consumes. What it cannot
  // do is overwrite what has not been read yet, so the excess is dropped and
  // counted here, before the router ever sees it.
  Fixture f;
  f.host.plug(3, 1, 1);
  f.tick();

  for (size_t i = 0; i < ESPMIDI_USB_HOST_PACKETS + 10; i++)
  {
    f.host.deliver(3, 0x0f, 0xf8);
  }
  assert(f.port.droppedPackets() == 10);

  f.tick();

  // There are two queues in a row and the router's is the narrower one, so a
  // burst this size is trimmed twice. Both losses are counted, in different
  // places: the port's ring says the sketch was too slow to read, and the
  // router's queue says the pipeline was too slow to run.
  assert(f.sink.statuses.size() == ESPMIDI_QUEUE_ENTRIES);
  assert(f.router.counters().received == ESPMIDI_USB_HOST_PACKETS);
  assert(f.router.counters().queueFull == ESPMIDI_USB_HOST_PACKETS - ESPMIDI_QUEUE_ENTRIES);

  // And it recovers: both queues are empty again.
  f.host.deliver(3, 0x09, 0x90, 60, 100);
  f.tick();
  assert(f.sink.statuses.size() == ESPMIDI_QUEUE_ENTRIES + 1);
}

void test_an_interrupted_dump_does_not_continue_into_the_next_one()
{
  Fixture f;
  f.host.plug(3, 1, 1, "SN-1");
  f.tick();

  f.host.deliver(3, 0x04, 0xf0, 0x7d, 0x01);
  f.tick();
  assert(f.sink.chunkEnds == 0);

  // Unplugged mid-dump. Rule 2: routing closes the stream downstream, so the
  // sound module on the other side is not left waiting for a terminator that the
  // keyboard can no longer send (docs/ROUTING.ja.md).
  f.host.unplug(3);
  f.tick();
  assert(f.sink.chunkEnds == 1);

  f.host.plug(3, 1, 1, "SN-1");
  f.tick();

  // A terminator from before the disconnect has no stream left to belong to, and
  // must not close the stream that was already closed for it.
  f.host.deliver(3, 0x05, 0xf7);
  f.tick();
  assert(f.sink.chunkEnds == 1);

  // A whole dump after the reconnect still works.
  f.host.deliver(3, 0x04, 0xf0, 0x7d, 0x02);
  f.host.deliver(3, 0x05, 0xf7);
  f.tick();
  assert(f.sink.chunkEnds == 2);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_a_device_gets_one_endpoint_and_a_port_per_cable);
  run(test_a_device_whose_interface_is_not_claimed_yet_is_looked_at_again);
  run(test_a_device_with_no_midi_is_ignored);
  run(test_a_hub_is_not_a_midi_device);
  run(test_received_packets_arrive_on_their_cable_s_port);
  run(test_a_packet_from_an_unknown_device_is_discarded);
  run(test_a_packet_on_an_undeclared_cable_is_counted);
  run(test_a_stream_is_concatenated_across_packets);
  run(test_a_disconnect_keeps_the_seat_and_stops_the_sending);
  run(test_a_device_with_a_serial_comes_back_to_the_same_seat);
  run(test_a_device_with_no_serial_gets_a_fresh_seat);
  run(test_several_devices_at_once);
  run(test_more_devices_than_there_is_room_for_are_counted);
  run(test_sending_puts_the_cable_in_the_header_and_addresses_the_device);
  run(test_a_stream_leaves_in_one_transfer);
  run(test_a_refused_transfer_is_counted);
  run(test_the_ring_bounds_what_one_pass_can_leave_behind);
  run(test_an_interrupted_dump_does_not_continue_into_the_next_one);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
