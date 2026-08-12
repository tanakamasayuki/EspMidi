// The BLE MIDI ports: the board as a device, and the board as a host.
//
// Two things here have no equivalent in the other ports. **Timestamps**: BLE MIDI
// is the only transport that carries one, and it has to arrive as
// Milliseconds13 and pass through untouched. **Reassembly**: EspBle takes a whole
// 0xF0..0xF7 message and does the splitting itself, so this is the one port that
// puts a stream back together before handing it over — and the one that has to
// refuse a dump too long to hold.
//
// Specification: docs/PORTS.ja.md.

#include <EspMidiEspBle.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
int g_ran = 0;

// EspBleMidiMessage, as much of it as the ports read.
struct FakeBleMessage
{
  uint16_t connectionId = 0;
  uint16_t timestampMs = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  uint8_t dataLength = 0;
  const uint8_t *raw = nullptr;
  size_t length = 0;
  bool sysEx = false;
  bool sysExStart = false;
  bool sysExEnd = false;
  const uint8_t *sysExData = nullptr;
  size_t sysExLength = 0;
};

FakeBleMessage bleShort(uint8_t *storage, uint16_t timestamp, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
{
  const int dataLength = espmidi::messageDataLength(status);
  FakeBleMessage message;
  message.timestampMs = timestamp;
  message.status = status;
  message.data1 = d1;
  message.data2 = d2;
  message.dataLength = static_cast<uint8_t>(dataLength < 0 ? 0 : dataLength);
  storage[0] = status;
  storage[1] = d1;
  storage[2] = d2;
  message.raw = storage;
  message.length = message.dataLength + 1u;
  return message;
}

FakeBleMessage bleChunk(const uint8_t *payload, size_t length, bool start, bool end, uint16_t timestamp = 0)
{
  FakeBleMessage message;
  message.timestampMs = timestamp;
  message.status = 0xf0;
  message.sysEx = true;
  message.sysExStart = start;
  message.sysExEnd = end;
  message.sysExData = payload;
  message.sysExLength = length;
  return message;
}

// --- The device side ------------------------------------------------------

struct FakeMidiDevice
{
  bool subscribed = false;
  bool sendOk = true;
  std::function<void(const FakeBleMessage &)> callback;
  std::vector<std::vector<uint8_t>> messages;
  std::vector<std::vector<uint8_t>> dumps;

  void onMessage(std::function<void(const FakeBleMessage &)> cb) { callback = std::move(cb); }
  bool ready() const { return subscribed; }

  bool sendMessage(const uint8_t *data, size_t length)
  {
    if (!sendOk)
    {
      return false;
    }
    messages.emplace_back(data, data + length);
    return true;
  }

  bool sendSysEx(const uint8_t *data, size_t length)
  {
    if (!sendOk)
    {
      return false;
    }
    dumps.emplace_back(data, data + length);
    return true;
  }

  void deliver(const FakeBleMessage &message)
  {
    if (callback)
    {
      callback(message);
    }
  }
};

using TestDevicePort = espmidi::BasicBleDevicePort<FakeMidiDevice>;

struct Sink
{
  std::vector<uint8_t> statuses;
  std::vector<uint16_t> ports;
  std::vector<uint16_t> timestamps;
  std::vector<espmidi::TimestampUnit> units;
  std::vector<uint8_t> payload;
  size_t chunkEnds = 0;

  static bool write(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    self->statuses.push_back(message.status);
    self->ports.push_back(message.port.value);
    self->timestamps.push_back(message.timestamp.value);
    self->units.push_back(message.timestamp.unit);
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

struct DeviceFixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  FakeMidiDevice midi;
  TestDevicePort port{router, midi};
  espmidi::AppPort app{router, "sketch"};
  Sink sink;

  DeviceFixture()
  {
    assert(port.begin("BLE MIDI"));
    app.onMessage([](void *context, const espmidi::Message &message) { Sink::write(context, message); }, &sink);
    router.addRoute(port.in(), app.out());
    router.addRoute(app.in(), port.out());
  }

  void connect()
  {
    midi.subscribed = true;
    port.update();
  }

  void disconnect()
  {
    midi.subscribed = false;
    port.update();
  }
};

void test_device_supplies_one_endpoint_with_two_ports()
{
  DeviceFixture f;

  assert(f.port.in().valid());
  assert(f.port.out().valid());

  espmidi::PortInfo info;
  assert(f.registry.portInfo(f.port.in().port, info));
  assert(info.transport == espmidi::Transport::BleDevice);
  assert(std::strcmp(info.name, "BLE MIDI") == 0);

  // Nobody has subscribed yet, so there is nothing on the other end.
  assert(!f.port.available());
  assert(info.state == espmidi::PortState::Disconnected);
}

void test_device_follows_the_subscription()
{
  DeviceFixture f;
  const espmidi::InPort in = f.port.in();

  f.connect();
  assert(f.port.available());
  assert(f.registry.portState(in.port) == espmidi::PortState::Available);

  f.disconnect();
  assert(!f.port.available());
  assert(f.registry.portState(in.port) == espmidi::PortState::Disconnected);

  // The seat survives, so the sketch's routes still point at it.
  f.connect();
  assert(f.port.in() == in);
}

void test_device_carries_the_timestamp_through()
{
  // BLE MIDI is the only transport with a timestamp. It is not interpreted, only
  // labelled: a stamp with no unit would be a number nobody could use.
  DeviceFixture f;
  f.connect();

  uint8_t storage[3] = {};
  f.midi.deliver(bleShort(storage, 1234, 0x90, 60, 100));
  f.router.update();

  assert(f.sink.statuses.size() == 1);
  assert(f.sink.timestamps[0] == 1234);
  assert(f.sink.units[0] == espmidi::TimestampUnit::Milliseconds13);
  assert(f.sink.ports[0] == f.port.in().port.value);
}

void test_device_received_stream_keeps_its_chunks()
{
  DeviceFixture f;
  f.connect();

  const uint8_t head[] = {0x7d, 0x01};
  const uint8_t tail[] = {0x02};
  f.midi.deliver(bleChunk(head, sizeof(head), true, false));
  f.midi.deliver(bleChunk(tail, sizeof(tail), false, true));
  f.router.update();

  // The framing bytes are EspBle's; what arrives is the payload.
  assert(f.sink.payload.size() == 3);
  assert(f.sink.payload[0] == 0x7d);
  assert(f.sink.payload[2] == 0x02);
  assert(f.sink.chunkEnds == 1);
}

void test_device_sends_short_messages()
{
  DeviceFixture f;
  f.connect();

  f.app.sendShort(0x90, 60, 100);
  f.router.update();

  assert(f.midi.messages.size() == 1);
  assert(f.midi.messages[0].size() == 3);
  assert(f.midi.messages[0][0] == 0x90);
  assert(f.midi.messages[0][1] == 60);
  assert(f.midi.messages[0][2] == 100);
}

void test_device_sends_nothing_before_anyone_subscribes()
{
  DeviceFixture f;

  f.app.sendShort(0x90, 60, 100);
  f.router.update();

  assert(f.midi.messages.empty());
  assert(f.router.counters().sendFailed == 1);
}

void test_device_reassembles_a_stream_before_sending_it()
{
  // EspBle takes the whole message and splits it itself, so the chunks have to be
  // put back together — with the framing bytes routing does not carry.
  DeviceFixture f;
  f.connect();

  const uint8_t head[] = {0x7d, 0x01};
  const uint8_t tail[] = {0x02, 0x03};
  espmidi::Message chunk;
  chunk.type = espmidi::MessageType::Data7;
  chunk.status = 0xf0;
  chunk.chunk = true;
  chunk.chunkStart = true;
  chunk.chunkData = head;
  chunk.chunkLength = sizeof(head);
  f.app.send(chunk);
  f.router.update();

  // Nothing has been sent yet: the stream is not complete.
  assert(f.midi.dumps.empty());

  chunk.chunkStart = false;
  chunk.chunkEnd = true;
  chunk.chunkData = tail;
  chunk.chunkLength = sizeof(tail);
  f.app.send(chunk);
  f.router.update();

  assert(f.midi.dumps.size() == 1);
  const std::vector<uint8_t> &dump = f.midi.dumps[0];
  assert(dump.size() == 6);
  assert(dump[0] == 0xf0);
  assert(dump[1] == 0x7d);
  assert(dump[4] == 0x03);
  assert(dump[5] == 0xf7);
}

void test_device_refuses_a_stream_it_cannot_hold()
{
  // Truncating a patch dump would send something that looks valid and is not.
  DeviceFixture f;
  f.connect();

  std::vector<uint8_t> payload(ESPMIDI_BLE_SYSEX_BYTES + 8, 0x01);
  espmidi::Message chunk;
  chunk.type = espmidi::MessageType::Data7;
  chunk.status = 0xf0;
  chunk.chunk = true;
  chunk.chunkStart = true;
  chunk.chunkData = payload.data();
  chunk.chunkLength = payload.size();
  f.app.send(chunk);

  espmidi::Message last = chunk;
  last.chunkStart = false;
  last.chunkEnd = true;
  last.chunkLength = 0;
  last.chunkData = nullptr;
  f.app.send(last);
  f.router.update();

  assert(f.midi.dumps.empty());
  assert(f.port.oversizedStreams() == 1);

  // And the next dump is unaffected: the buffer was not left half full.
  const uint8_t small[] = {0x7d};
  espmidi::Message ok;
  ok.type = espmidi::MessageType::Data7;
  ok.status = 0xf0;
  ok.chunk = true;
  ok.chunkStart = true;
  ok.chunkEnd = true;
  ok.chunkData = small;
  ok.chunkLength = sizeof(small);
  f.app.send(ok);
  f.router.update();
  assert(f.midi.dumps.size() == 1);
  assert(f.midi.dumps[0].size() == 3);
}

void test_device_a_refused_notification_is_counted()
{
  DeviceFixture f;
  f.connect();
  f.midi.sendOk = false;

  f.app.sendShort(0x90, 60, 100);
  f.router.update();

  assert(f.router.counters().sendFailed == 1);
}

// --- The host side --------------------------------------------------------

struct FakeConnection
{
  uint16_t id = 0;
  std::string peerAddress;
};

struct FakeMidiHost
{
  std::function<void(const FakeBleMessage &)> callback;
  std::vector<uint16_t> discovered;
  std::vector<uint16_t> readyIds;
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> messages;
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> dumps;
  bool discoverOk = true;
  bool sendOk = true;

  // EspBle names it onMidiMessage() on the host and onMessage() on the device.
  void onMidiMessage(std::function<void(const FakeBleMessage &)> cb) { callback = std::move(cb); }

  bool discover(uint16_t connectionId, uint32_t = 10000)
  {
    if (!discoverOk)
    {
      return false;
    }
    discovered.push_back(connectionId);
    return true;
  }

  bool ready(uint16_t connectionId) const
  {
    for (uint16_t id : readyIds)
    {
      if (id == connectionId)
      {
        return true;
      }
    }
    return false;
  }

  bool sendMessage(uint16_t connectionId, const uint8_t *data, size_t length)
  {
    if (!sendOk)
    {
      return false;
    }
    messages.emplace_back(connectionId, std::vector<uint8_t>(data, data + length));
    return true;
  }

  bool sendSysEx(uint16_t connectionId, const uint8_t *data, size_t length)
  {
    if (!sendOk)
    {
      return false;
    }
    dumps.emplace_back(connectionId, std::vector<uint8_t>(data, data + length));
    return true;
  }

  void deliver(uint16_t connectionId, FakeBleMessage message)
  {
    message.connectionId = connectionId;
    if (callback)
    {
      callback(message);
    }
  }
};

struct FakeBle
{
  std::vector<FakeConnection> connections;
  std::function<void(const FakeConnection &)> onConnect;
  std::function<void(const FakeConnection &)> onDisconnect;

  int addConnectedListener(std::function<void(const FakeConnection &)> cb)
  {
    onConnect = std::move(cb);
    return 0;
  }

  int addDisconnectedListener(std::function<void(const FakeConnection &)> cb)
  {
    onDisconnect = std::move(cb);
    return 0;
  }

  bool connection(uint16_t connectionId, FakeConnection &out) const
  {
    for (const FakeConnection &candidate : connections)
    {
      if (candidate.id == connectionId)
      {
        out = candidate;
        return true;
      }
    }
    return false;
  }

  void connect(uint16_t id, const char *address)
  {
    FakeConnection connection;
    connection.id = id;
    connection.peerAddress = address;
    connections.push_back(connection);
    if (onConnect)
    {
      onConnect(connection);
    }
  }

  void disconnect(uint16_t id)
  {
    for (size_t i = 0; i < connections.size(); i++)
    {
      if (connections[i].id == id)
      {
        const FakeConnection gone = connections[i];
        connections.erase(connections.begin() + static_cast<long>(i));
        if (onDisconnect)
        {
          onDisconnect(gone);
        }
        return;
      }
    }
  }
};

using TestHostPort = espmidi::BasicBleHostPort<FakeMidiHost, FakeBle>;

struct HostFixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  FakeMidiHost midi;
  FakeBle ble;
  TestHostPort port{router, midi, ble};
  Sink sink;

  HostFixture()
  {
    assert(port.begin());

    espmidi::EndpointIdentity identity;
    identity.transport = espmidi::Transport::Application;
    identity.index = 9;
    const espmidi::EndpointId endpoint = registry.attachEndpoint(identity, "observer");
    const espmidi::OutPort out = registry.attachOutPort(endpoint, 0);
    router.setOutputSink(out, &Sink::write, &sink);
    router.addRoute(espmidi::InGroup::all(), out);
  }

  void tick()
  {
    port.update();
    router.update();
  }

  // A connection that has been discovered and subscribed, which is when it
  // becomes usable.
  void bring(uint16_t id, const char *address)
  {
    ble.connect(id, address);
    tick(); // records the connection and starts discovery
    midi.readyIds.push_back(id);
    tick(); // finds it ready and seats it
  }
};

void test_host_seats_a_connection_once_it_is_ready()
{
  HostFixture f;
  f.ble.connect(1, "aa:bb:cc:dd:ee:ff");
  f.tick();

  // Connected but not usable: the MIDI service has not been found yet, so there
  // is nothing to route to.
  assert(f.port.deviceCount() == 0);
  assert(f.midi.discovered.size() == 1);

  f.midi.readyIds.push_back(1);
  f.tick();

  assert(f.port.deviceCount() == 1);
  assert(f.port.in(1).valid());
  assert(f.port.out(1).valid());
  assert(f.registry.portState(f.port.in(1).port) == espmidi::PortState::Available);
}

void test_host_received_messages_carry_their_connection_s_port()
{
  HostFixture f;
  f.bring(1, "aa:bb:cc:dd:ee:ff");
  f.bring(2, "11:22:33:44:55:66");

  uint8_t storage[3] = {};
  f.midi.deliver(1, bleShort(storage, 100, 0x90, 60, 100));
  f.router.update();
  uint8_t other[3] = {};
  f.midi.deliver(2, bleShort(other, 200, 0xb0, 7, 64));
  f.router.update();

  assert(f.sink.ports.size() == 2);
  assert(f.sink.ports[0] == f.port.in(1).port.value);
  assert(f.sink.timestamps[0] == 100);
  assert(f.sink.ports[1] == f.port.in(2).port.value);
  assert(f.sink.timestamps[1] == 200);
}

void test_host_a_message_for_an_unknown_connection_is_dropped()
{
  HostFixture f;
  f.bring(1, "aa:bb:cc:dd:ee:ff");

  uint8_t storage[3] = {};
  f.midi.deliver(9, bleShort(storage, 0, 0x90, 60, 100));
  f.router.update();

  assert(f.sink.statuses.empty());
}

void test_host_connection_zero_is_a_real_connection()
{
  // The published slots start empty, and an empty slot must not look like
  // connection 0.
  HostFixture f;
  uint8_t storage[3] = {};
  f.midi.deliver(0, bleShort(storage, 0, 0x90, 60, 100));
  f.router.update();
  assert(f.sink.statuses.empty());

  f.bring(0, "aa:bb:cc:dd:ee:ff");
  f.midi.deliver(0, bleShort(storage, 0, 0x90, 60, 100));
  f.router.update();
  assert(f.sink.statuses.size() == 1);
}

void test_host_a_disconnect_keeps_the_seat_and_stops_the_sending()
{
  HostFixture f;
  f.bring(1, "aa:bb:cc:dd:ee:ff");
  const espmidi::InPort in = f.port.in(1);

  espmidi::AppPort app{f.router, "sketch"};
  f.router.addRoute(app.in(), f.port.out(1));
  app.sendShort(0x90, 60, 100);
  f.router.update();
  assert(f.midi.messages.size() == 1);

  f.ble.disconnect(1);
  f.tick();

  assert(f.port.deviceCount() == 0);
  assert(f.registry.portState(in.port) == espmidi::PortState::Disconnected);

  app.sendShort(0x90, 62, 100);
  f.router.update();
  assert(f.midi.messages.size() == 1);
  assert(f.router.counters().sendFailed >= 1);

  // And a message that was already in flight cannot reach the seat either. The
  // observer watches every input including the application port, so what matters
  // is that nothing new arrives from the keyboard.
  const size_t before = f.sink.statuses.size();
  uint8_t storage[3] = {};
  f.midi.deliver(1, bleShort(storage, 0, 0x90, 60, 100));
  f.router.update();
  assert(f.sink.statuses.size() == before);
}

void test_host_a_keyboard_comes_back_to_its_seat_by_address()
{
  // A BLE keyboard gets a different connection id every time; the address is what
  // says it is the same keyboard.
  HostFixture f;
  f.bring(1, "aa:bb:cc:dd:ee:ff");
  const espmidi::InPort in = f.port.in(1);
  f.ble.disconnect(1);
  f.tick();

  f.bring(5, "aa:bb:cc:dd:ee:ff");

  assert(f.port.in(5) == in);
  assert(f.registry.endpointCount() == 2); // the keyboard and the observer
}

void test_host_a_different_keyboard_gets_a_different_seat()
{
  HostFixture f;
  f.bring(1, "aa:bb:cc:dd:ee:ff");
  const espmidi::InPort first = f.port.in(1);
  f.ble.disconnect(1);
  f.tick();

  f.bring(2, "11:22:33:44:55:66");

  assert(f.port.in(2) != first);
  assert(f.registry.endpointCount() == 3);
}

void test_host_sends_to_the_right_connection()
{
  HostFixture f;
  f.bring(1, "aa:bb:cc:dd:ee:ff");
  f.bring(2, "11:22:33:44:55:66");

  espmidi::AppPort app{f.router, "sketch"};
  f.router.addRoute(app.in(), f.port.out(2));
  app.sendShort(0xb0, 7, 64);
  f.router.update();

  assert(f.midi.messages.size() == 1);
  assert(f.midi.messages[0].first == 2);
}

void test_host_reassembles_a_stream_per_connection()
{
  HostFixture f;
  f.bring(1, "aa:bb:cc:dd:ee:ff");

  espmidi::AppPort app{f.router, "sketch"};
  f.router.addRoute(app.in(), f.port.out(1));

  const uint8_t payload[] = {0x7d, 0x01};
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

  assert(f.midi.dumps.size() == 1);
  assert(f.midi.dumps[0].first == 1);
  assert(f.midi.dumps[0].second.size() == 4);
  assert(f.midi.dumps[0].second[0] == 0xf0);
  assert(f.midi.dumps[0].second[3] == 0xf7);
}

void test_host_more_connections_than_there_is_room_for_are_counted()
{
  HostFixture f;
  for (uint16_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS + 2; i++)
  {
    f.ble.connect(static_cast<uint16_t>(100 + i), "aa:bb:cc:dd:ee:ff");
  }
  f.tick();

  assert(f.port.refusedConnections() == 2);
}

void test_host_events_beyond_the_ring_are_counted()
{
  // Connections come through a ring the BLE task writes and update() reads, so a
  // burst of them arriving between two passes of loop() is bounded.
  HostFixture f;
  for (uint16_t i = 0; i < ESPMIDI_BLE_EVENTS + 3; i++)
  {
    f.ble.connect(static_cast<uint16_t>(200 + i), "aa:bb:cc:dd:ee:ff");
  }

  assert(f.port.droppedEvents() == 3);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_device_supplies_one_endpoint_with_two_ports);
  run(test_device_follows_the_subscription);
  run(test_device_carries_the_timestamp_through);
  run(test_device_received_stream_keeps_its_chunks);
  run(test_device_sends_short_messages);
  run(test_device_sends_nothing_before_anyone_subscribes);
  run(test_device_reassembles_a_stream_before_sending_it);
  run(test_device_refuses_a_stream_it_cannot_hold);
  run(test_device_a_refused_notification_is_counted);

  run(test_host_seats_a_connection_once_it_is_ready);
  run(test_host_received_messages_carry_their_connection_s_port);
  run(test_host_a_message_for_an_unknown_connection_is_dropped);
  run(test_host_connection_zero_is_a_real_connection);
  run(test_host_a_disconnect_keeps_the_seat_and_stops_the_sending);
  run(test_host_a_keyboard_comes_back_to_its_seat_by_address);
  run(test_host_a_different_keyboard_gets_a_different_seat);
  run(test_host_sends_to_the_right_connection);
  run(test_host_reassembles_a_stream_per_connection);
  run(test_host_more_connections_than_there_is_room_for_are_counted);
  run(test_host_events_beyond_the_ring_are_counted);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
