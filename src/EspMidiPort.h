// The port model: endpoints, ports, seats and port groups.
//
// Specification: docs/DATA_MODEL.ja.md. The two ideas that shape this file:
//
//   Endpoint > Port. An endpoint is a connection — one USB device, one BLE link,
//   one UART — and a port is one routing coordinate inside it, which is a USB
//   cable today and a MIDI 2.0 group later. An endpoint carries at most 16 ports
//   per direction because both of those fields are four bits wide.
//
//   A handle is a seat, not a device. Disconnecting does not invalidate a port;
//   it changes its state. Routing set up against a port survives the device being
//   unplugged, and a device that comes back is matched to the seat it had before
//   by its identity. Without this, unplugging a keyboard would silently discard
//   every route pointing at it.
//
// A consequence worth stating: ports are never removed. There is no PortRemoved
// event because a seat outlives the device that occupied it, so consumers only
// ever handle "a seat appeared" and "a seat changed state".
//
// Storage is fixed-size and allocation-free. The limits are compile-time knobs
// so a sketch that bridges two UARTs does not pay for sixteen USB devices.

#ifndef ESPMIDI_PORT_H
#define ESPMIDI_PORT_H

#include "EspMidiMessage.h"

#include <string.h>

#ifndef ESPMIDI_MAX_ENDPOINTS
#define ESPMIDI_MAX_ENDPOINTS 8
#endif

#ifndef ESPMIDI_MAX_PORTS
#define ESPMIDI_MAX_PORTS 32
#endif

#ifndef ESPMIDI_MAX_PORT_GROUPS
#define ESPMIDI_MAX_PORT_GROUPS 8
#endif

#ifndef ESPMIDI_NAME_MAX
#define ESPMIDI_NAME_MAX 32
#endif

// USB serial strings and BLE addresses. Long enough for a printed BLE address
// and for the serials devices actually ship.
#ifndef ESPMIDI_SERIAL_MAX
#define ESPMIDI_SERIAL_MAX 24
#endif

#ifndef ESPMIDI_MAX_PORT_LISTENERS
#define ESPMIDI_MAX_PORT_LISTENERS 4
#endif

namespace espmidi
{

enum class Transport : uint8_t
{
  Unknown = 0,
  Uart,
  UsbDevice, // this board seen as a MIDI device
  UsbHost,   // a device plugged into this board
  // Split for the same reason USB is: this board's own BLE MIDI service is fixed
  // by the board, while a device it connects to is discovered and identified by
  // its address.
  BleDevice,
  BleHost,
  // A port the sketch itself drives: it injects messages and receives them.
  // Control mapping helpers (buttons, encoders) and monitors are built on it,
  // and it participates in routing exactly like a transport-backed port.
  Application,
};

enum class Direction : uint8_t
{
  In = 0,  // into EspMidi: the board receives
  Out = 1, // out of EspMidi: the board sends
};

enum class PortState : uint8_t
{
  Unconnected = 0, // the seat exists but nothing has ever occupied it
  Disconnected,    // something occupied it and went away; routing is kept
  Available,       // usable now
};

// --- Handles --------------------------------------------------------------

struct EndpointId
{
  static constexpr uint16_t Invalid = 0xffff;
  uint16_t value = Invalid;
  bool valid() const { return value != Invalid; }
};

inline bool operator==(const EndpointId &a, const EndpointId &b) { return a.value == b.value; }
inline bool operator!=(const EndpointId &a, const EndpointId &b) { return !(a == b); }

// An input and an output are different things to route, so they are different
// types: addRoute(in, out) cannot be called the wrong way round. Both convert to
// the PortId a Message carries, but not to each other.
struct InPort
{
  PortId port;
  bool valid() const { return port.valid(); }
  operator PortId() const { return port; }
};

struct OutPort
{
  PortId port;
  bool valid() const { return port.valid(); }
  operator PortId() const { return port; }
};

inline bool operator==(const InPort &a, const InPort &b) { return a.port == b.port; }
inline bool operator!=(const InPort &a, const InPort &b) { return !(a == b); }
inline bool operator==(const OutPort &a, const OutPort &b) { return a.port == b.port; }
inline bool operator!=(const OutPort &a, const OutPort &b) { return !(a == b); }

// Port groups. "All inputs" and "all outputs" are reserved handles rather than
// groups someone has to build and keep up to date, so a route to every output
// keeps working when a device is plugged in later.
struct InGroup
{
  static constexpr uint16_t Invalid = 0xffff;
  static constexpr uint16_t AllValue = 0xfffe;
  uint16_t value = Invalid;
  bool valid() const { return value != Invalid; }
  bool isAll() const { return value == AllValue; }
  static InGroup all() { return InGroup{AllValue}; }
};

struct OutGroup
{
  static constexpr uint16_t Invalid = 0xffff;
  static constexpr uint16_t AllValue = 0xfffe;
  uint16_t value = Invalid;
  bool valid() const { return value != Invalid; }
  bool isAll() const { return value == AllValue; }
  static OutGroup all() { return OutGroup{AllValue}; }
};

inline bool operator==(const InGroup &a, const InGroup &b) { return a.value == b.value; }
inline bool operator!=(const InGroup &a, const InGroup &b) { return !(a == b); }
inline bool operator==(const OutGroup &a, const OutGroup &b) { return a.value == b.value; }
inline bool operator!=(const OutGroup &a, const OutGroup &b) { return !(a == b); }

// --- Identity -------------------------------------------------------------

// What makes a device the same device across a disconnect. A seat is only
// re-used when the identity matches, so two indistinguishable devices get two
// seats rather than fighting over one (docs/DECISIONS.ja.md, decision 3).
struct EndpointIdentity
{
  Transport transport = Transport::Unknown;
  // Distinguishes fixed endpoints of the same transport: UART1 from UART2, or
  // one USB Device MIDI interface from another.
  uint8_t index = 0;
  uint16_t vendorId = 0;
  uint16_t productId = 0;
  // USB serial string, or the printed BLE address. Empty when the device does
  // not supply one.
  char serial[ESPMIDI_SERIAL_MAX] = {};

  bool hasSerial() const { return serial[0] != '\0'; }

  // A transport whose endpoints are fixed by the board rather than discovered.
  // These are always identifiable: there is only one UART1.
  bool isStatic() const
  {
    return transport == Transport::Uart || transport == Transport::UsbDevice ||
           transport == Transport::BleDevice || transport == Transport::Application;
  }

  // Whether this identity can be recognised again after a disconnect. A USB
  // device with no serial cannot, so it gets a fresh seat every time rather
  // than possibly stealing another device's routing.
  bool identifiable() const
  {
    if (transport == Transport::Unknown)
    {
      return false;
    }
    return isStatic() || hasSerial();
  }

  bool matches(const EndpointIdentity &other) const
  {
    if (transport != other.transport || index != other.index)
    {
      return false;
    }
    if (isStatic())
    {
      return true;
    }
    if (!hasSerial() || !other.hasSerial())
    {
      return false; // unidentifiable: never the same seat
    }
    return vendorId == other.vendorId && productId == other.productId &&
           strncmp(serial, other.serial, ESPMIDI_SERIAL_MAX) == 0;
  }
};

// --- Read-only views ------------------------------------------------------

struct EndpointInfo
{
  EndpointId id;
  EndpointIdentity identity;
  const char *name = "";
  PortState state = PortState::Unconnected;
};

struct PortInfo
{
  PortId id;
  EndpointId endpoint;
  Direction direction = Direction::In;
  // The port's coordinate inside its endpoint: a USB cable number today, a UMP
  // group later. 0..15.
  uint8_t index = 0;
  PortState state = PortState::Unconnected;
  Transport transport = Transport::Unknown;
  // The endpoint's name. Per-port names would come from USB jack strings, which
  // neither transport library reads yet (docs/LIBRARY_REQUESTS.ja.md).
  const char *name = "";
};

// --- Events ---------------------------------------------------------------

enum class PortEventType : uint8_t
{
  PortAdded = 0,
  PortStateChanged,
  EndpointStateChanged,
};

struct PortEvent
{
  PortEventType type = PortEventType::PortAdded;
  EndpointId endpoint;
  PortId port; // invalid for EndpointStateChanged
  PortState state = PortState::Unconnected;
};

// A plain function pointer with a context rather than std::function: a capturing
// std::function allocates, and this notification path runs from a port adapter
// while a device is being enumerated. A capture-less lambda converts to this,
// and anything else passes `this` as the context.
using PortEventCallback = void (*)(void *context, const PortEvent &event);

// --- The registry ---------------------------------------------------------

class PortRegistry
{
public:
  static constexpr size_t MaxEndpoints = ESPMIDI_MAX_ENDPOINTS;
  static constexpr size_t MaxPorts = ESPMIDI_MAX_PORTS;
  static constexpr size_t MaxGroups = ESPMIDI_MAX_PORT_GROUPS;

  // Finds the seat for this identity and marks it available, creating the seat
  // the first time. Called by a port adapter when a device appears — and again
  // on every reconnect, which is what returns the device to the routing it had
  // before.
  //
  // Returns an invalid id when the registry is full, or when the identity has no
  // transport.
  EndpointId attachEndpoint(const EndpointIdentity &identity, const char *name = nullptr)
  {
    if (identity.transport == Transport::Unknown)
    {
      return EndpointId();
    }

    EndpointId found = findEndpoint(identity);
    if (!found.valid())
    {
      found = allocateEndpoint(identity);
      if (!found.valid())
      {
        return found;
      }
    }

    EndpointSlot &slot = endpoints_[found.value];
    if (name)
    {
      copyName(slot.name, name);
    }
    setEndpointState(found, PortState::Available);
    return found;
  }

  // The device went away. The seat and every port on it stay, so routes keep
  // pointing at them; only the state changes.
  bool detachEndpoint(EndpointId endpoint)
  {
    if (!validEndpoint(endpoint))
    {
      return false;
    }
    setEndpointState(endpoint, PortState::Disconnected);
    return true;
  }

  // Finds the seat for a port of this endpoint, creating it the first time.
  // `index` is the port's coordinate inside the endpoint — a USB cable number —
  // so it comes from the transport rather than from a caller picking a slot.
  InPort attachInPort(EndpointId endpoint, uint8_t index)
  {
    return InPort{attachPort(endpoint, Direction::In, index)};
  }

  OutPort attachOutPort(EndpointId endpoint, uint8_t index)
  {
    return OutPort{attachPort(endpoint, Direction::Out, index)};
  }

  // --- Queries ------------------------------------------------------------

  size_t endpointCount() const { return endpointCount_; }
  size_t portCount() const { return portCount_; }

  bool endpointInfo(EndpointId endpoint, EndpointInfo &info) const
  {
    if (!validEndpoint(endpoint))
    {
      return false;
    }
    const EndpointSlot &slot = endpoints_[endpoint.value];
    info.id = endpoint;
    info.identity = slot.identity;
    info.name = slot.name;
    info.state = slot.state;
    return true;
  }

  bool portInfo(PortId port, PortInfo &info) const
  {
    if (!validPort(port))
    {
      return false;
    }
    const PortSlot &slot = ports_[port.value];
    const EndpointSlot &endpoint = endpoints_[slot.endpoint];
    info.id = port;
    info.endpoint = EndpointId{slot.endpoint};
    info.direction = slot.direction;
    info.index = slot.index;
    info.state = slot.state;
    info.transport = endpoint.identity.transport;
    info.name = endpoint.name;
    return true;
  }

  PortState portState(PortId port) const
  {
    return validPort(port) ? ports_[port.value].state : PortState::Unconnected;
  }

  bool portAvailable(PortId port) const { return portState(port) == PortState::Available; }

  Direction portDirection(PortId port) const
  {
    return validPort(port) ? ports_[port.value].direction : Direction::In;
  }

  EndpointId portEndpoint(PortId port) const
  {
    return validPort(port) ? EndpointId{ports_[port.value].endpoint} : EndpointId();
  }

  // Loop prevention starts here: by default a message that came in on an
  // endpoint is not sent back out of it (docs/ROUTING.ja.md).
  bool sameEndpoint(PortId a, PortId b) const
  {
    if (!validPort(a) || !validPort(b))
    {
      return false;
    }
    return ports_[a.value].endpoint == ports_[b.value].endpoint;
  }

  // Enumeration by position, for walking every seat. The position is not a
  // handle and is not stable in the way a PortId is; it is only valid until the
  // next attach.
  PortId portAt(size_t position) const
  {
    return position < portCount_ ? PortId{static_cast<uint16_t>(position)} : PortId();
  }

  EndpointId endpointAt(size_t position) const
  {
    return position < endpointCount_ ? EndpointId{static_cast<uint16_t>(position)} : EndpointId();
  }

  // --- Groups -------------------------------------------------------------

  InGroup addInGroup(const char *name) { return InGroup{addGroup(Direction::In, name)}; }
  OutGroup addOutGroup(const char *name) { return OutGroup{addGroup(Direction::Out, name)}; }

  bool addToGroup(InGroup group, InPort port) { return addMember(group.value, Direction::In, port.port); }
  bool addToGroup(OutGroup group, OutPort port) { return addMember(group.value, Direction::Out, port.port); }

  bool removeFromGroup(InGroup group, InPort port) { return removeMember(group.value, port.port); }
  bool removeFromGroup(OutGroup group, OutPort port) { return removeMember(group.value, port.port); }

  bool groupContains(InGroup group, InPort port) const { return contains(group.value, Direction::In, port.port); }
  bool groupContains(OutGroup group, OutPort port) const { return contains(group.value, Direction::Out, port.port); }

  const char *groupName(InGroup group) const { return groupNameOf(group.value, Direction::In); }
  const char *groupName(OutGroup group) const { return groupNameOf(group.value, Direction::Out); }

  size_t groupCount() const { return groupCount_; }

  // --- Listeners ----------------------------------------------------------

  // Registered listeners are called when a seat appears or changes state. They
  // run in the context of whichever port adapter reported the change, which for
  // USB and BLE is the transport's own task.
  bool addListener(PortEventCallback callback, void *context = nullptr)
  {
    if (!callback || listenerCount_ >= ESPMIDI_MAX_PORT_LISTENERS)
    {
      return false;
    }
    listeners_[listenerCount_].callback = callback;
    listeners_[listenerCount_].context = context;
    listenerCount_++;
    return true;
  }

  void clearListeners() { listenerCount_ = 0; }

private:
  struct EndpointSlot
  {
    EndpointIdentity identity;
    char name[ESPMIDI_NAME_MAX] = {};
    PortState state = PortState::Unconnected;
  };

  struct PortSlot
  {
    uint16_t endpoint = 0;
    Direction direction = Direction::In;
    uint8_t index = 0;
    PortState state = PortState::Unconnected;
  };

  struct GroupSlot
  {
    Direction direction = Direction::In;
    char name[ESPMIDI_NAME_MAX] = {};
    uint32_t members[(MaxPorts + 31) / 32] = {};
  };

  struct Listener
  {
    PortEventCallback callback = nullptr;
    void *context = nullptr;
  };

  static void copyName(char *dst, const char *src)
  {
    if (!src)
    {
      dst[0] = '\0';
      return;
    }
    size_t i = 0;
    for (; i + 1 < ESPMIDI_NAME_MAX && src[i] != '\0'; i++)
    {
      dst[i] = src[i];
    }
    dst[i] = '\0';
  }

  bool validEndpoint(EndpointId endpoint) const
  {
    return endpoint.valid() && endpoint.value < endpointCount_;
  }

  bool validPort(PortId port) const { return port.valid() && port.value < portCount_; }

  EndpointId findEndpoint(const EndpointIdentity &identity) const
  {
    if (!identity.identifiable())
    {
      return EndpointId();
    }
    for (size_t i = 0; i < endpointCount_; i++)
    {
      if (endpoints_[i].identity.matches(identity))
      {
        return EndpointId{static_cast<uint16_t>(i)};
      }
    }
    return EndpointId();
  }

  EndpointId allocateEndpoint(const EndpointIdentity &identity)
  {
    if (endpointCount_ >= MaxEndpoints)
    {
      return EndpointId();
    }
    EndpointSlot &slot = endpoints_[endpointCount_];
    slot = EndpointSlot();
    slot.identity = identity;
    slot.state = PortState::Unconnected;
    const EndpointId id{static_cast<uint16_t>(endpointCount_)};
    endpointCount_++;
    return id;
  }

  PortId attachPort(EndpointId endpoint, Direction direction, uint8_t index)
  {
    if (!validEndpoint(endpoint) || index >= MaxPortsPerEndpoint)
    {
      return PortId();
    }

    for (size_t i = 0; i < portCount_; i++)
    {
      const PortSlot &slot = ports_[i];
      if (slot.endpoint == endpoint.value && slot.direction == direction && slot.index == index)
      {
        // The seat already exists; a reconnect finds it here rather than making
        // a second one.
        const PortId existing{static_cast<uint16_t>(i)};
        setPortState(existing, endpoints_[endpoint.value].state);
        return existing;
      }
    }

    if (portCount_ >= MaxPorts)
    {
      return PortId();
    }
    PortSlot &slot = ports_[portCount_];
    slot = PortSlot();
    slot.endpoint = endpoint.value;
    slot.direction = direction;
    slot.index = index;
    slot.state = endpoints_[endpoint.value].state;
    const PortId id{static_cast<uint16_t>(portCount_)};
    portCount_++;

    PortEvent event;
    event.type = PortEventType::PortAdded;
    event.endpoint = endpoint;
    event.port = id;
    event.state = slot.state;
    notify(event);
    return id;
  }

  void setEndpointState(EndpointId endpoint, PortState state)
  {
    EndpointSlot &slot = endpoints_[endpoint.value];
    if (slot.state == state)
    {
      return;
    }
    slot.state = state;

    PortEvent event;
    event.type = PortEventType::EndpointStateChanged;
    event.endpoint = endpoint;
    event.state = state;
    notify(event);

    // Every port of an endpoint follows it: a connection is what comes and goes,
    // not an individual cable.
    for (size_t i = 0; i < portCount_; i++)
    {
      if (ports_[i].endpoint == endpoint.value)
      {
        setPortState(PortId{static_cast<uint16_t>(i)}, state);
      }
    }
  }

  void setPortState(PortId port, PortState state)
  {
    PortSlot &slot = ports_[port.value];
    if (slot.state == state)
    {
      return;
    }
    slot.state = state;

    PortEvent event;
    event.type = PortEventType::PortStateChanged;
    event.endpoint = EndpointId{slot.endpoint};
    event.port = port;
    event.state = state;
    notify(event);
  }

  void notify(const PortEvent &event)
  {
    for (size_t i = 0; i < listenerCount_; i++)
    {
      listeners_[i].callback(listeners_[i].context, event);
    }
  }

  uint16_t addGroup(Direction direction, const char *name)
  {
    if (groupCount_ >= MaxGroups)
    {
      return InGroup::Invalid;
    }
    GroupSlot &slot = groups_[groupCount_];
    slot = GroupSlot();
    slot.direction = direction;
    copyName(slot.name, name);
    return static_cast<uint16_t>(groupCount_++);
  }

  bool validGroup(uint16_t group, Direction direction) const
  {
    return group < groupCount_ && groups_[group].direction == direction;
  }

  bool addMember(uint16_t group, Direction direction, PortId port)
  {
    // The reserved "all" groups have no membership to edit: everything of the
    // right direction is already in them, including ports added later.
    if (!validGroup(group, direction) || !validPort(port) || ports_[port.value].direction != direction)
    {
      return false;
    }
    groups_[group].members[port.value / 32] |= (1u << (port.value % 32));
    return true;
  }

  bool removeMember(uint16_t group, PortId port)
  {
    if (group >= groupCount_ || !validPort(port))
    {
      return false;
    }
    groups_[group].members[port.value / 32] &= ~(1u << (port.value % 32));
    return true;
  }

  bool contains(uint16_t group, Direction direction, PortId port) const
  {
    if (!validPort(port) || ports_[port.value].direction != direction)
    {
      return false;
    }
    if (group == InGroup::AllValue)
    {
      return true;
    }
    if (!validGroup(group, direction))
    {
      return false;
    }
    return (groups_[group].members[port.value / 32] & (1u << (port.value % 32))) != 0;
  }

  const char *groupNameOf(uint16_t group, Direction direction) const
  {
    if (group == InGroup::AllValue)
    {
      return direction == Direction::In ? "all inputs" : "all outputs";
    }
    return validGroup(group, direction) ? groups_[group].name : "";
  }

  EndpointSlot endpoints_[MaxEndpoints];
  PortSlot ports_[MaxPorts];
  GroupSlot groups_[MaxGroups];
  Listener listeners_[ESPMIDI_MAX_PORT_LISTENERS];
  size_t endpointCount_ = 0;
  size_t portCount_ = 0;
  size_t groupCount_ = 0;
  size_t listenerCount_ = 0;
};

} // namespace espmidi

#endif // ESPMIDI_PORT_H
