// The port model: endpoints, the seat behaviour of handles, state propagation,
// groups and events.
//
// Specification: docs/DATA_MODEL.ja.md, decision 3 in docs/DECISIONS.ja.md.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
int g_ran = 0;

espmidi::EndpointIdentity uart(uint8_t index)
{
  espmidi::EndpointIdentity identity;
  identity.transport = espmidi::Transport::Uart;
  identity.index = index;
  return identity;
}

espmidi::EndpointIdentity usbDevice(uint16_t vid, uint16_t pid, const char *serial)
{
  espmidi::EndpointIdentity identity;
  identity.transport = espmidi::Transport::UsbHost;
  identity.vendorId = vid;
  identity.productId = pid;
  if (serial)
  {
    std::strncpy(identity.serial, serial, ESPMIDI_SERIAL_MAX - 1);
  }
  return identity;
}

void test_static_endpoint_and_ports()
{
  espmidi::PortRegistry registry;
  assert(registry.endpointCount() == 0);
  assert(registry.portCount() == 0);

  const espmidi::EndpointId endpoint = registry.attachEndpoint(uart(1), "UART1");
  assert(endpoint.valid());
  assert(registry.endpointCount() == 1);

  const espmidi::InPort in = registry.attachInPort(endpoint, 0);
  const espmidi::OutPort out = registry.attachOutPort(endpoint, 0);
  assert(in.valid() && out.valid());
  assert(registry.portCount() == 2);

  // In and out are separate seats even at the same index, because they are
  // separate things to route.
  assert(in.port != out.port);
  assert(registry.portDirection(in) == espmidi::Direction::In);
  assert(registry.portDirection(out) == espmidi::Direction::Out);

  espmidi::PortInfo info;
  assert(registry.portInfo(in, info));
  assert(info.endpoint == endpoint);
  assert(info.index == 0);
  assert(info.transport == espmidi::Transport::Uart);
  assert(std::strcmp(info.name, "UART1") == 0);
  assert(info.state == espmidi::PortState::Available);

  espmidi::EndpointInfo endpointInfo;
  assert(registry.endpointInfo(endpoint, endpointInfo));
  assert(endpointInfo.state == espmidi::PortState::Available);
  assert(endpointInfo.identity.transport == espmidi::Transport::Uart);
}

void test_attach_is_idempotent()
{
  // A port adapter re-attaching the same coordinate gets the same seat, which is
  // what makes reconnect handling a plain re-run of the connect path.
  espmidi::PortRegistry registry;
  const espmidi::EndpointId endpoint = registry.attachEndpoint(uart(1), "UART1");

  const espmidi::InPort first = registry.attachInPort(endpoint, 0);
  const espmidi::InPort again = registry.attachInPort(endpoint, 0);
  assert(first == again);
  assert(registry.portCount() == 1);

  // A different cable is a different seat.
  const espmidi::InPort other = registry.attachInPort(endpoint, 1);
  assert(other != first);
  assert(registry.portCount() == 2);
}

void test_ports_are_bounded_by_the_endpoint()
{
  // Sixteen ports per endpoint, because a USB cable and a UMP group are both
  // four bits wide.
  espmidi::PortRegistry registry;
  const espmidi::EndpointId endpoint = registry.attachEndpoint(uart(1));

  assert(registry.attachInPort(endpoint, 15).valid());
  assert(!registry.attachInPort(endpoint, 16).valid());
  assert(!registry.attachInPort(endpoint, 255).valid());

  // An invalid endpoint yields no port rather than a port on nothing.
  assert(!registry.attachInPort(espmidi::EndpointId(), 0).valid());
}

void test_seat_survives_disconnect()
{
  // The point of the whole model: unplugging must not invalidate handles, or
  // every route pointing at the device would be silently discarded.
  espmidi::PortRegistry registry;
  const espmidi::EndpointId endpoint =
      registry.attachEndpoint(usbDevice(0x0582, 0x0100, "SN12345"), "A-88");
  const espmidi::InPort in = registry.attachInPort(endpoint, 0);
  const espmidi::OutPort out = registry.attachOutPort(endpoint, 0);
  assert(registry.portAvailable(in));

  assert(registry.detachEndpoint(endpoint));

  // Still the same handles, still countable, just not usable.
  assert(registry.portCount() == 2);
  assert(registry.portState(in) == espmidi::PortState::Disconnected);
  assert(registry.portState(out) == espmidi::PortState::Disconnected);
  assert(!registry.portAvailable(in));

  espmidi::PortInfo info;
  assert(registry.portInfo(in, info));
  assert(info.endpoint == endpoint);
}

void test_reconnect_returns_to_the_same_seat()
{
  espmidi::PortRegistry registry;
  const espmidi::EndpointId first =
      registry.attachEndpoint(usbDevice(0x0582, 0x0100, "SN12345"), "A-88");
  const espmidi::InPort in = registry.attachInPort(first, 0);
  registry.detachEndpoint(first);

  // The same device comes back. USB addresses change across a re-plug, so the
  // match is on identity, not on where it landed.
  const espmidi::EndpointId again =
      registry.attachEndpoint(usbDevice(0x0582, 0x0100, "SN12345"), "A-88");
  assert(again == first);
  assert(registry.endpointCount() == 1);

  const espmidi::InPort inAgain = registry.attachInPort(again, 0);
  assert(inAgain == in);
  assert(registry.portCount() == 1);
  assert(registry.portAvailable(in));
}

void test_a_different_device_gets_a_different_seat()
{
  espmidi::PortRegistry registry;
  const espmidi::EndpointId a = registry.attachEndpoint(usbDevice(0x0582, 0x0100, "SN1"));
  const espmidi::EndpointId b = registry.attachEndpoint(usbDevice(0x0582, 0x0100, "SN2"));
  assert(a != b);
  assert(registry.endpointCount() == 2);

  // Same serial, different product: not the same device.
  const espmidi::EndpointId c = registry.attachEndpoint(usbDevice(0x0582, 0x0999, "SN1"));
  assert(c != a);
  assert(registry.endpointCount() == 3);
}

void test_unidentifiable_devices_get_fresh_seats()
{
  // A device with no serial cannot be recognised again. Reusing a seat on a
  // guess would hand one device the routing set up for another, so each
  // connection gets its own seat instead.
  espmidi::PortRegistry registry;
  const espmidi::EndpointIdentity anonymous = usbDevice(0x1234, 0x5678, nullptr);
  assert(!anonymous.identifiable());

  const espmidi::EndpointId first = registry.attachEndpoint(anonymous, "Cheap Keyboard");
  registry.detachEndpoint(first);
  const espmidi::EndpointId second = registry.attachEndpoint(anonymous, "Cheap Keyboard");

  assert(first != second);
  assert(registry.endpointCount() == 2);

  // A fixed endpoint is always identifiable: there is only one UART1.
  assert(uart(1).identifiable());
  assert(uart(1).matches(uart(1)));
  assert(!uart(1).matches(uart(2)));
}

void test_state_follows_the_endpoint()
{
  // A connection is what comes and goes, not an individual cable, so every port
  // of an endpoint moves with it.
  espmidi::PortRegistry registry;
  const espmidi::EndpointId endpoint = registry.attachEndpoint(usbDevice(1, 2, "S"), "Device");
  const espmidi::InPort in0 = registry.attachInPort(endpoint, 0);
  const espmidi::InPort in1 = registry.attachInPort(endpoint, 1);
  const espmidi::OutPort out0 = registry.attachOutPort(endpoint, 0);

  registry.detachEndpoint(endpoint);
  assert(registry.portState(in0) == espmidi::PortState::Disconnected);
  assert(registry.portState(in1) == espmidi::PortState::Disconnected);
  assert(registry.portState(out0) == espmidi::PortState::Disconnected);

  registry.attachEndpoint(usbDevice(1, 2, "S"), "Device");
  assert(registry.portAvailable(in0));
  assert(registry.portAvailable(in1));
  assert(registry.portAvailable(out0));
}

void test_same_endpoint_detects_the_loop_case()
{
  // The default loop rule needs exactly this: is this output on the endpoint the
  // message came in on?
  espmidi::PortRegistry registry;
  const espmidi::EndpointId keyboard = registry.attachEndpoint(usbDevice(1, 1, "K"), "Keyboard");
  const espmidi::EndpointId synth = registry.attachEndpoint(usbDevice(2, 2, "S"), "Synth");

  const espmidi::InPort keyboardIn = registry.attachInPort(keyboard, 0);
  const espmidi::OutPort keyboardOut = registry.attachOutPort(keyboard, 0);
  const espmidi::OutPort synthOut = registry.attachOutPort(synth, 0);

  assert(registry.sameEndpoint(keyboardIn, keyboardOut));
  assert(!registry.sameEndpoint(keyboardIn, synthOut));
  assert(!registry.sameEndpoint(keyboardIn, espmidi::PortId()));
}

void test_enumeration()
{
  espmidi::PortRegistry registry;
  const espmidi::EndpointId endpoint = registry.attachEndpoint(uart(1), "UART1");
  const espmidi::InPort in = registry.attachInPort(endpoint, 0);
  const espmidi::OutPort out = registry.attachOutPort(endpoint, 0);

  assert(registry.endpointAt(0) == endpoint);
  assert(!registry.endpointAt(1).valid());
  assert(registry.portAt(0) == in.port);
  assert(registry.portAt(1) == out.port);
  assert(!registry.portAt(2).valid());
}

void test_groups()
{
  espmidi::PortRegistry registry;
  const espmidi::EndpointId a = registry.attachEndpoint(uart(1), "UART1");
  const espmidi::EndpointId b = registry.attachEndpoint(uart(2), "UART2");
  const espmidi::OutPort outA = registry.attachOutPort(a, 0);
  const espmidi::OutPort outB = registry.attachOutPort(b, 0);
  const espmidi::InPort inA = registry.attachInPort(a, 0);

  const espmidi::OutGroup synths = registry.addOutGroup("synths");
  assert(synths.valid());
  assert(std::strcmp(registry.groupName(synths), "synths") == 0);

  assert(registry.addToGroup(synths, outA));
  assert(registry.groupContains(synths, outA));
  assert(!registry.groupContains(synths, outB));

  assert(registry.addToGroup(synths, outB));
  assert(registry.groupContains(synths, outB));

  assert(registry.removeFromGroup(synths, outA));
  assert(!registry.groupContains(synths, outA));

  // Groups are typed by direction, so an input cannot join an output group.
  const espmidi::InGroup controls = registry.addInGroup("controls");
  assert(registry.addToGroup(controls, inA));
  assert(registry.groupContains(controls, inA));
}

void test_all_groups_are_reserved_and_always_current()
{
  // "All outputs" is a reserved handle rather than a group someone maintains, so
  // a device plugged in later is already in it and a route to every output keeps
  // working.
  espmidi::PortRegistry registry;
  const espmidi::EndpointId a = registry.attachEndpoint(uart(1), "UART1");
  const espmidi::OutPort outA = registry.attachOutPort(a, 0);
  const espmidi::InPort inA = registry.attachInPort(a, 0);

  const espmidi::OutGroup allOut = espmidi::OutGroup::all();
  const espmidi::InGroup allIn = espmidi::InGroup::all();
  assert(allOut.valid() && allOut.isAll());
  assert(registry.groupContains(allOut, outA));
  assert(registry.groupContains(allIn, inA));

  // A port that appears later is in it without anything being updated.
  const espmidi::EndpointId b = registry.attachEndpoint(usbDevice(1, 2, "S"), "Synth");
  const espmidi::OutPort outB = registry.attachOutPort(b, 0);
  assert(registry.groupContains(allOut, outB));

  // Direction still holds: an output is not in "all inputs".
  assert(!registry.groupContains(allIn, espmidi::InPort{outB.port}));

  assert(std::strcmp(registry.groupName(allOut), "all outputs") == 0);
  assert(std::strcmp(registry.groupName(allIn), "all inputs") == 0);
}

struct EventLog
{
  static constexpr size_t Capacity = 32;
  espmidi::PortEvent items[Capacity] = {};
  size_t count = 0;

  static void handle(void *context, const espmidi::PortEvent &event)
  {
    EventLog *log = static_cast<EventLog *>(context);
    assert(log->count < Capacity);
    log->items[log->count++] = event;
  }

  size_t countOf(espmidi::PortEventType type) const
  {
    size_t total = 0;
    for (size_t i = 0; i < count; i++)
    {
      if (items[i].type == type)
      {
        total++;
      }
    }
    return total;
  }

  void clear() { count = 0; }
};

void test_events()
{
  espmidi::PortRegistry registry;
  EventLog log;
  assert(registry.addListener(&EventLog::handle, &log));

  const espmidi::EndpointId endpoint = registry.attachEndpoint(usbDevice(1, 2, "S"), "Synth");
  // Attaching an endpoint with no ports yet changes its state, which is
  // reported even though there is nothing to route to.
  assert(log.countOf(espmidi::PortEventType::EndpointStateChanged) == 1);

  log.clear();
  const espmidi::InPort in = registry.attachInPort(endpoint, 0);
  assert(log.countOf(espmidi::PortEventType::PortAdded) == 1);
  assert(log.items[0].port == in.port);
  assert(log.items[0].state == espmidi::PortState::Available);

  // Re-attaching an existing seat is not a new port.
  log.clear();
  registry.attachInPort(endpoint, 0);
  assert(log.countOf(espmidi::PortEventType::PortAdded) == 0);

  log.clear();
  registry.detachEndpoint(endpoint);
  assert(log.countOf(espmidi::PortEventType::EndpointStateChanged) == 1);
  assert(log.countOf(espmidi::PortEventType::PortStateChanged) == 1);

  // Detaching twice reports nothing: the state did not change.
  log.clear();
  registry.detachEndpoint(endpoint);
  assert(log.count == 0);

  registry.clearListeners();
  log.clear();
  registry.attachEndpoint(usbDevice(1, 2, "S"), "Synth");
  assert(log.count == 0);
}

void test_capacity_limits_fail_rather_than_overflow()
{
  espmidi::PortRegistry registry;

  // Fill the endpoint table. Each identity is distinct so none of them share a
  // seat.
  for (size_t i = 0; i < espmidi::PortRegistry::MaxEndpoints; i++)
  {
    assert(registry.attachEndpoint(uart(static_cast<uint8_t>(i))).valid());
  }
  assert(registry.endpointCount() == espmidi::PortRegistry::MaxEndpoints);
  assert(!registry.attachEndpoint(uart(200)).valid());

  // An identity with no transport is refused rather than stored as Unknown.
  assert(!registry.attachEndpoint(espmidi::EndpointIdentity()).valid());

  // Queries about handles that were never issued are answered, not crashed on.
  espmidi::PortInfo info;
  assert(!registry.portInfo(espmidi::PortId(), info));
  espmidi::EndpointInfo endpointInfo;
  assert(!registry.endpointInfo(espmidi::EndpointId(), endpointInfo));
  assert(!registry.detachEndpoint(espmidi::EndpointId()));
  assert(registry.portState(espmidi::PortId()) == espmidi::PortState::Unconnected);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_static_endpoint_and_ports);
  run(test_attach_is_idempotent);
  run(test_ports_are_bounded_by_the_endpoint);
  run(test_seat_survives_disconnect);
  run(test_reconnect_returns_to_the_same_seat);
  run(test_a_different_device_gets_a_different_seat);
  run(test_unidentifiable_devices_get_fresh_seats);
  run(test_state_follows_the_endpoint);
  run(test_same_endpoint_detects_the_loop_case);
  run(test_enumeration);
  run(test_groups);
  run(test_all_groups_are_reserved_and_always_current);
  run(test_events);
  run(test_capacity_limits_fail_rather_than_overflow);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
