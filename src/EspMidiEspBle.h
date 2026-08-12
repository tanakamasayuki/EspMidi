// BLE MIDI ports, on EspBle.
//
// Specification: docs/PORTS.ja.md.
//
// Two ports live here, because BLE MIDI is one profile seen from two sides:
//
//   espmidi::BleDevicePort   the board is the MIDI device a phone connects to
//   espmidi::BleHostPort     the board connects to BLE MIDI keyboards itself
//
// Neither goes near raw GATT (docs/DECISIONS.ja.md, decision 2). The sketch owns
// the BLE stack, creates an EspBleMidiDevice or EspBleMidiHost, and hands it over;
// the packet format, running status and the splitting of a dump across BLE packets
// all belong to EspBle. What is left here is the seat, the state, and the
// translation between two message structures that already look alike.
//
// **These are the only ports with timestamps.** BLE MIDI carries a 13-bit
// millisecond stamp, which arrives as TimestampUnit::Milliseconds13 and is passed
// through untouched: this library does not interpret time (docs/REQUIREMENTS.ja.md).
// The stamp on the way out is EspBle's, taken when the packet is built.
//
// **Received messages go straight into the router from the BLE task.** That is
// safe — Router::receive() is the one entry point that has to be, and it is
// (docs/CORE_DESIGN.ja.md) — and it is also what keeps a dump zero-copy: the
// chunk still points into NimBLE's own buffer while the queue copies it. The only
// state the callback shares with update() is one atomic port handle per
// connection.
//
// Include this header only in a sketch that wants a BLE port; EspMidi.h does not
// pull it in.

#ifndef ESPMIDI_ESP_BLE_H
#define ESPMIDI_ESP_BLE_H

#include "EspMidi.h"

#include <atomic>

#if defined(ARDUINO)
#include <EspBle.h>
#include <EspBleMidi.h>
#include <EspBleMidiProfile.h>
#endif

// BLE MIDI connections that can have seats at once, on the host side.
#ifndef ESPMIDI_MAX_BLE_CONNECTIONS
#define ESPMIDI_MAX_BLE_CONNECTIONS 4
#endif

// Bytes of a data stream held while it is reassembled, framing included.
//
// This buffer exists because EspBle takes a whole 0xF0..0xF7 message and does the
// splitting itself, while routing delivers a stream in chunks. It is the one place
// in the library where a dump is put back together, and the default matches the
// largest EspBle will accept. A longer dump is refused and counted rather than
// truncated — a half-sent patch is worse than none.
#ifndef ESPMIDI_BLE_SYSEX_BYTES
#define ESPMIDI_BLE_SYSEX_BYTES 320
#endif

// Connection events the BLE task can leave for update() to apply.
#ifndef ESPMIDI_BLE_EVENTS
#define ESPMIDI_BLE_EVENTS 8
#endif

namespace espmidi
{

// The connection structure is deduced from the transport's own signature rather
// than named, so a sketch passes its plain EspBle object and a test on the host
// passes a stand-in.
template <typename T, typename I, typename P>
P espBleConnectionType(bool (T::*)(I, P &) const);

// --- Shared between the two sides ----------------------------------------

// Turns EspBle's MIDI message into this library's. The two structures were
// designed to line up, so this is a rename rather than a conversion; the pointers
// are carried over as they are, which is what keeps a dump from being copied.
template <typename BleMessage>
inline Message messageFromBle(const BleMessage &source, PortId port)
{
  Message message;
  message.port = port;
  message.timestamp.value = source.timestampMs;
  message.timestamp.unit = TimestampUnit::Milliseconds13;

  if (source.sysEx)
  {
    message.type = MessageType::Data7;
    message.status = 0xf0;
    message.chunk = true;
    message.chunkStart = source.sysExStart;
    message.chunkEnd = source.sysExEnd;
    message.chunkData = source.sysExData;
    message.chunkLength = source.sysExLength;
    return message;
  }

  message.type = messageTypeForStatus(source.status);
  message.status = source.status;
  message.data1 = source.data1;
  message.data2 = source.data2;
  message.dataLength = source.dataLength;
  message.raw = source.raw;
  message.length = source.length;
  return message;
}

// Reassembles a data stream so it can be handed over whole.
//
// EspBle wants the complete 0xF0..0xF7 message and splits it across packets
// itself; routing hands over chunks. Something has to bridge that, and doing it
// here keeps the BLE packet format where it belongs — in EspBle.
class BleSysExBuffer
{
public:
  void reset()
  {
    length_ = 0;
    open_ = false;
    overflowed_ = false;
  }

  bool open() const { return open_; }
  bool overflowed() const { return overflowed_; }
  const uint8_t *data() const { return buffer_; }
  size_t length() const { return length_; }

  // Adds a chunk. Returns true when the stream is complete and can be sent.
  bool add(const Message &message)
  {
    if (message.chunkStart)
    {
      length_ = 0;
      open_ = true;
      overflowed_ = false;
      push(0xf0);
    }
    else if (!open_)
    {
      return false;
    }

    for (size_t i = 0; i < message.chunkLength; i++)
    {
      push(message.chunkData[i]);
    }

    if (!message.chunkEnd)
    {
      return false;
    }
    push(0xf7);
    open_ = false;
    return !overflowed_;
  }

private:
  void push(uint8_t byte)
  {
    if (length_ >= ESPMIDI_BLE_SYSEX_BYTES)
    {
      overflowed_ = true;
      return;
    }
    buffer_[length_++] = byte;
  }

  uint8_t buffer_[ESPMIDI_BLE_SYSEX_BYTES] = {};
  size_t length_ = 0;
  bool open_ = false;
  bool overflowed_ = false;
};

// --- The device side ------------------------------------------------------

// The board as a BLE MIDI device: one endpoint, one input port and one output
// port. A phone or a PC connects to it.
//
// Templated on the profile object so the port's own behaviour is fixed by tests on
// the host, as with every other port here.
template <typename MidiType>
class BasicBleDevicePort
{
public:
  BasicBleDevicePort(Router &router, MidiType &midi, uint8_t index = 0)
      : router_(router), midi_(midi), index_(index)
  {
  }

  // Supplies the endpoint and its two ports, and starts listening. Call it after
  // the profile object's own begin(). Idempotent.
  bool begin(const char *name = "BLE MIDI")
  {
    EndpointIdentity identity;
    identity.transport = Transport::BleDevice;
    identity.index = index_;

    endpoint_ = router_.registry().attachEndpoint(identity, name);
    if (!endpoint_.valid())
    {
      return false;
    }
    in_ = router_.registry().attachInPort(endpoint_, 0);
    out_ = router_.registry().attachOutPort(endpoint_, 0);
    if (!in_.valid() || !out_.valid())
    {
      return false;
    }
    if (!router_.setOutputSink(out_, &BasicBleDevicePort::sendFrom, this))
    {
      return false;
    }

    // Published for the BLE task, which is the only thing it shares with this one.
    inPortValue_.store(in_.port.value, std::memory_order_release);

    if (!listening_)
    {
      listening_ = true;
      // This is the profile object's single primary callback, so **a sketch that
      // wants to see BLE MIDI itself should read it through routing** rather than
      // registering its own.
      midi_.onMessage([this](const auto &message) { onBleMessage(message); });
    }

    // A host has to connect and subscribe before anything can be sent, so the
    // endpoint starts disconnected and update() reports what it finds.
    available_ = false;
    router_.registry().detachEndpoint(endpoint_);
    return true;
  }

  // Follows the subscription state. Call it from loop(), next to the BLE stack's
  // own update() and Router::update().
  void update()
  {
    if (!endpoint_.valid())
    {
      return;
    }
    const bool up = midi_.ready();
    if (up == available_)
    {
      return;
    }
    available_ = up;
    if (up)
    {
      EndpointIdentity identity;
      identity.transport = Transport::BleDevice;
      identity.index = index_;
      router_.registry().attachEndpoint(identity);
    }
    else
    {
      // Nobody left to send a terminator to.
      sysEx_.reset();
      router_.registry().detachEndpoint(endpoint_);
    }
  }

  void end()
  {
    sysEx_.reset();
    available_ = false;
    if (endpoint_.valid())
    {
      router_.registry().detachEndpoint(endpoint_);
    }
  }

  EndpointId endpoint() const { return endpoint_; }
  InPort in() const { return in_; }
  OutPort out() const { return out_; }
  bool available() const { return available_; }

  // Data streams refused because they were longer than ESPMIDI_BLE_SYSEX_BYTES.
  uint32_t oversizedStreams() const { return oversized_; }

private:
  template <typename BleMessage>
  void onBleMessage(const BleMessage &source)
  {
    const uint16_t port = inPortValue_.load(std::memory_order_acquire);
    if (port == PortId::Invalid)
    {
      return;
    }
    // Straight into the queue from the BLE task. The chunk still points into
    // NimBLE's buffer, and the queue is what copies it.
    router_.receive(messageFromBle(source, PortId{port}));
  }

  static bool sendFrom(void *context, const Message &message)
  {
    return static_cast<BasicBleDevicePort *>(context)->send(message);
  }

  bool send(const Message &message)
  {
    if (!available_)
    {
      return false;
    }

    if (message.chunk)
    {
      if (!sysEx_.add(message))
      {
        if (sysEx_.overflowed() && message.chunkEnd)
        {
          oversized_++;
          sysEx_.reset();
        }
        // Not an error yet: the stream is still being collected. Reporting
        // success here is what lets the rest of it arrive.
        return !sysEx_.overflowed();
      }
      return midi_.sendSysEx(sysEx_.data(), sysEx_.length());
    }

    uint8_t bytes[MaxShortMessageBytes] = {};
    const size_t length = serializeShortMessage(message, bytes, sizeof(bytes));
    if (length == 0)
    {
      return false;
    }
    return midi_.sendMessage(bytes, length);
  }

  Router &router_;
  MidiType &midi_;
  uint8_t index_ = 0;
  EndpointId endpoint_;
  InPort in_;
  OutPort out_;
  BleSysExBuffer sysEx_;
  std::atomic<uint16_t> inPortValue_{PortId::Invalid};
  uint32_t oversized_ = 0;
  bool available_ = false;
  bool listening_ = false;
};

// --- The host side --------------------------------------------------------

// The board as a BLE MIDI host: an endpoint per connected device, each with one
// input port and one output port.
//
// Unlike the USB host port, connections cannot be listed by index, so they arrive
// through EspBle's listeners. Those run on the BLE task, so all they do is record
// the connection id in a ring; the seats are made from update().
template <typename MidiType, typename BleType>
class BasicBleHostPort
{
public:
  using Connection = decltype(espBleConnectionType(&BleType::connection));

  BasicBleHostPort(Router &router, MidiType &midi, BleType &ble) : router_(router), midi_(midi), ble_(ble)
  {
    // Connection id 0 is a real id, so the published slots have to start at
    // something that is not one.
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      connectionIds_[i].store(InvalidConnection, std::memory_order_relaxed);
      inPortValues_[i].store(PortId::Invalid, std::memory_order_relaxed);
    }
  }

  BasicBleHostPort(const BasicBleHostPort &) = delete;
  BasicBleHostPort &operator=(const BasicBleHostPort &) = delete;

  // Installs the listeners. Call it after the BLE stack has started.
  bool begin()
  {
    if (started_)
    {
      return true;
    }
    started_ = true;
    // EspBle names this one onMidiMessage() on the host and onMessage() on the
    // device. Both are that object's single primary callback, so **a sketch that
    // wants to see BLE MIDI itself should read it through routing** rather than
    // registering its own.
    midi_.onMidiMessage([this](const auto &message) { onBleMessage(message); });
    // Additional listeners, not the primary onConnected(), so the sketch keeps
    // its own view of connections.
    ble_.addConnectedListener([this](const auto &connection) { record(Kind::Connected, connection.id); });
    ble_.addDisconnectedListener([this](const auto &connection) { record(Kind::Disconnected, connection.id); });
    return true;
  }

  void end()
  {
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      if (slots_[i].used)
      {
        release(slots_[i]);
      }
    }
    started_ = false;
  }

  // Applies what the BLE task recorded, then looks for connections whose MIDI
  // service has finished being discovered. Call it from loop().
  void update()
  {
    if (!started_)
    {
      return;
    }

    drainEvents();

    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      Slot &slot = slots_[i];
      if (!slot.used || slot.seated)
      {
        continue;
      }
      if (!slot.discovering)
      {
        // Finding the MIDI service is the MIDI-specific part of bringing a
        // connection up, so it belongs to the port rather than to the sketch.
        slot.discovering = midi_.discover(slot.connectionId);
        continue;
      }
      if (midi_.ready(slot.connectionId))
      {
        seat(slot);
      }
    }
  }

  size_t deviceCount() const
  {
    size_t count = 0;
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      count += (slots_[i].used && slots_[i].seated) ? 1 : 0;
    }
    return count;
  }

  InPort in(uint16_t connectionId) const
  {
    const Slot *slot = find(connectionId);
    return slot && slot->seated ? slot->in : InPort();
  }

  OutPort out(uint16_t connectionId) const
  {
    const Slot *slot = find(connectionId);
    return slot && slot->seated ? slot->out : OutPort();
  }

  EndpointId endpointFor(uint16_t connectionId) const
  {
    const Slot *slot = find(connectionId);
    return slot ? slot->endpoint : EndpointId();
  }

  // The nth seated connection's id, for walking what is connected.
  uint16_t connectionAt(size_t index) const
  {
    size_t seen = 0;
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      if (!slots_[i].used || !slots_[i].seated)
      {
        continue;
      }
      if (seen == index)
      {
        return slots_[i].connectionId;
      }
      seen++;
    }
    return 0;
  }

  uint32_t oversizedStreams() const { return oversized_; }
  uint32_t droppedEvents() const { return droppedEvents_.load(std::memory_order_relaxed); }
  uint32_t refusedConnections() const { return refused_; }

private:
  enum class Kind : uint8_t
  {
    Connected,
    Disconnected,
  };

  struct Event
  {
    Kind kind = Kind::Connected;
    uint16_t connectionId = 0;
    std::atomic<bool> ready{false};
  };

  struct Slot
  {
    bool used = false;
    bool discovering = false;
    bool seated = false;
    uint16_t connectionId = 0;
    EndpointId endpoint;
    InPort in;
    OutPort out;
    BleSysExBuffer sysEx;
    BasicBleHostPort *self = nullptr;
  };

  // --- The BLE task's half ------------------------------------------------

  void record(Kind kind, uint16_t connectionId)
  {
    uint32_t reserved = tail_.load(std::memory_order_relaxed);
    for (;;)
    {
      const uint32_t head = head_.load(std::memory_order_acquire);
      if (reserved - head >= ESPMIDI_BLE_EVENTS)
      {
        droppedEvents_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      if (tail_.compare_exchange_weak(reserved, reserved + 1, std::memory_order_relaxed))
      {
        break;
      }
    }
    Event &event = events_[reserved % ESPMIDI_BLE_EVENTS];
    event.kind = kind;
    event.connectionId = connectionId;
    event.ready.store(true, std::memory_order_release);
  }

  template <typename BleMessage>
  void onBleMessage(const BleMessage &source)
  {
    // The published handle is the only thing this shares with update(). A stale
    // read can only mean a message for a connection that has just gone away,
    // which routing then drops.
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      if (connectionIds_[i].load(std::memory_order_acquire) != source.connectionId)
      {
        continue;
      }
      const uint16_t port = inPortValues_[i].load(std::memory_order_acquire);
      if (port != PortId::Invalid)
      {
        router_.receive(messageFromBle(source, PortId{port}));
      }
      return;
    }
  }

  // --- The sketch task's half ---------------------------------------------

  void drainEvents()
  {
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    uint32_t head = head_.load(std::memory_order_relaxed);

    while (head != tail)
    {
      Event &event = events_[head % ESPMIDI_BLE_EVENTS];
      if (!event.ready.load(std::memory_order_acquire))
      {
        break;
      }
      apply(event.kind, event.connectionId);
      event.ready.store(false, std::memory_order_relaxed);
      head++;
      head_.store(head, std::memory_order_release);
    }
  }

  void apply(Kind kind, uint16_t connectionId)
  {
    Slot *slot = find(connectionId);
    if (kind == Kind::Disconnected)
    {
      if (slot)
      {
        release(*slot);
      }
      return;
    }
    if (slot)
    {
      return; // already known
    }
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      if (!slots_[i].used)
      {
        slots_[i] = Slot();
        slots_[i].used = true;
        slots_[i].connectionId = connectionId;
        slots_[i].self = this;
        return;
      }
    }
    refused_++;
  }

  void seat(Slot &slot)
  {
    Connection connection;
    EndpointIdentity identity;
    identity.transport = Transport::BleHost;
    identity.index = 0;
    // The BLE address is the identity: a keyboard that comes back to a different
    // connection id is still the same keyboard, and gets its seat back.
    if (ble_.connection(slot.connectionId, connection))
    {
      copyAddress(identity.serial, connection.peerAddress);
    }

    const EndpointId endpoint = router_.registry().attachEndpoint(identity, "BLE MIDI");
    if (!endpoint.valid())
    {
      refused_++;
      slot.used = false;
      return;
    }

    slot.endpoint = endpoint;
    slot.in = router_.registry().attachInPort(endpoint, 0);
    slot.out = router_.registry().attachOutPort(endpoint, 0);
    slot.seated = true;

    const size_t index = static_cast<size_t>(&slot - slots_);
    router_.setOutputSink(slot.out, &BasicBleHostPort::sendFrom, &slots_[index]);

    // Published last, so the BLE task never sees a handle before its seat exists.
    inPortValues_[index].store(slot.in.port.value, std::memory_order_release);
    connectionIds_[index].store(slot.connectionId, std::memory_order_release);
  }

  void release(Slot &slot)
  {
    const size_t index = static_cast<size_t>(&slot - slots_);
    // Withdrawn first: after this the BLE task cannot reach the seat at all.
    connectionIds_[index].store(InvalidConnection, std::memory_order_release);
    inPortValues_[index].store(PortId::Invalid, std::memory_order_release);

    if (slot.endpoint.valid())
    {
      router_.registry().detachEndpoint(slot.endpoint);
    }
    slot.used = false;
    slot.seated = false;
    slot.discovering = false;
    slot.sysEx.reset();
  }

  static bool sendFrom(void *context, const Message &message)
  {
    Slot *slot = static_cast<Slot *>(context);
    return slot->self->send(*slot, message);
  }

  bool send(Slot &slot, const Message &message)
  {
    if (!slot.used || !slot.seated)
    {
      return false;
    }

    if (message.chunk)
    {
      if (!slot.sysEx.add(message))
      {
        if (slot.sysEx.overflowed() && message.chunkEnd)
        {
          oversized_++;
          slot.sysEx.reset();
        }
        return !slot.sysEx.overflowed();
      }
      return midi_.sendSysEx(slot.connectionId, slot.sysEx.data(), slot.sysEx.length());
    }

    uint8_t bytes[MaxShortMessageBytes] = {};
    const size_t length = serializeShortMessage(message, bytes, sizeof(bytes));
    if (length == 0)
    {
      return false;
    }
    return midi_.sendMessage(slot.connectionId, bytes, length);
  }

  Slot *find(uint16_t connectionId)
  {
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      if (slots_[i].used && slots_[i].connectionId == connectionId)
      {
        return &slots_[i];
      }
    }
    return nullptr;
  }

  const Slot *find(uint16_t connectionId) const
  {
    for (size_t i = 0; i < ESPMIDI_MAX_BLE_CONNECTIONS; i++)
    {
      if (slots_[i].used && slots_[i].connectionId == connectionId)
      {
        return &slots_[i];
      }
    }
    return nullptr;
  }

  // Whatever string type the transport uses, as long as it answers c_str().
  template <typename StringType>
  static void copyAddress(char *dst, const StringType &address)
  {
    const char *src = address.c_str();
    size_t i = 0;
    if (src)
    {
      for (; i + 1 < ESPMIDI_SERIAL_MAX && src[i] != '\0'; i++)
      {
        dst[i] = src[i];
      }
    }
    dst[i] = '\0';
  }

  static constexpr uint16_t InvalidConnection = 0xffff;

  Router &router_;
  MidiType &midi_;
  BleType &ble_;
  Slot slots_[ESPMIDI_MAX_BLE_CONNECTIONS];
  Event events_[ESPMIDI_BLE_EVENTS];
  std::atomic<uint16_t> connectionIds_[ESPMIDI_MAX_BLE_CONNECTIONS];
  std::atomic<uint16_t> inPortValues_[ESPMIDI_MAX_BLE_CONNECTIONS];
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
  std::atomic<uint32_t> droppedEvents_{0};
  uint32_t oversized_ = 0;
  uint32_t refused_ = 0;
  bool started_ = false;
};

#if defined(ARDUINO)
using BleDevicePort = BasicBleDevicePort<EspBleMidiDevice>;
using BleHostPort = BasicBleHostPort<EspBleMidiHost, EspBle>;
#endif

} // namespace espmidi

#endif // ESPMIDI_ESP_BLE_H
