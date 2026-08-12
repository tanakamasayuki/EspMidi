// Every API the guides name, compiled.
//
// docs/GUIDE.ja.md and docs/MIDI_BASICS.ja.md show code. A rename in the library
// would leave those snippets quietly wrong, and a guide that names a method that
// no longer exists is worse than no guide at all: the reader trusts it.
//
// So this file uses each of them once. It is not testing behaviour — the subjects
// next door do that — it is testing that **the guides still describe this
// library**. If it stops compiling, a document needs updating.
//
// Only the transport-independent snippets are here. The ones that create a port
// (espmidi::UartPort and friends) are covered by examples_compile, which builds
// the sketches the guides point at.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>

namespace
{
int g_ran = 0;

void onMidi(void *, const espmidi::Message &message)
{
  // GUIDE, section 2: what a receiving callback looks like.
  if (message.chunk)
  {
    return;
  }
  (void)message.command();
  (void)message.channel();
  (void)message.status;
  (void)message.data1;
  (void)message.data2;
}

espmidi::PortRegistry registry;

espmidi::Message clockMessage()
{
  static uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message message;
  espmidi::buildShortMessage(message, bytes, 0xf8);
  return message;
}

void onPortEvent(void *, const espmidi::PortEvent &event)
{
  // GUIDE, section 6: the notification, and the two event kinds it has.
  espmidi::PortInfo info;
  if (registry.portInfo(event.port, info))
  {
    (void)info.name;
    (void)info.state;
    (void)info.direction;
  }
  assert(event.type == espmidi::PortEventType::PortAdded ||
         event.type == espmidi::PortEventType::PortStateChanged);
}

void test_the_guide_still_describes_this_library()
{
  espmidi::Router router{registry};

  espmidi::EndpointIdentity identity;
  identity.transport = espmidi::Transport::Uart;
  const espmidi::EndpointId endpoint = registry.attachEndpoint(identity, "din");
  const espmidi::InPort in = registry.attachInPort(endpoint, 0);
  const espmidi::OutPort out = registry.attachOutPort(endpoint, 0);

  espmidi::AppPort sketch(router, "sketch");

  // Section 1: inject, then update().
  sketch.sendShort(0x90, 60, 100);
  router.update();
  sketch.sendShort(0x80, 60, 0);

  // Section 2: receive through an application port.
  sketch.onMessage(onMidi);

  // Section 4: a route with a declared filter and transform.
  const espmidi::Route route = router.addRoute(in, out);

  espmidi::Filter lower;
  lower.kinds = espmidi::KindNotes;
  lower.noteMin = 36;
  lower.noteMax = 59;
  assert(router.setRouteFilter(route, lower));

  espmidi::Transform toBass;
  toBass.transpose = -12;
  toBass.channel = 1;
  assert(router.setRouteTransform(route, toBass));

  // Section 4: the same rules on a port instead of a route.
  assert(router.setInPortFilter(in, lower));
  assert(router.setOutPortTransform(out, toBass));

  // Section 5: groups, and the deliberate exception to the loop rule.
  router.addRoute(espmidi::InGroup::all(), out);
  router.addRoute(in, espmidi::OutGroup::all());
  assert(router.setRouteAllowSameEndpoint(route, true));

  // Section 6: seats, states and notifications.
  (void)registry.portAvailable(out.port);
  registry.addListener(onPortEvent);

  // Section 7: the control helpers, told the reading and the time.
  espmidi::Analog knob(sketch);
  espmidi::Button button(sketch);
  knob.update(2048);
  button.update(true, 100);
  knob.config().hysteresis = 16;

  // "Filtering the clock out of a monitor", quoted in both guides.
  espmidi::Filter quiet;
  quiet.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindSystemRealTime);
  assert(!quiet.accepts(clockMessage()));

  // Troubleshooting: the counters the guide tells the reader to print, by name.
  const espmidi::RouterCounters counters = router.counters();
  (void)counters.received;
  (void)counters.delivered;
  (void)counters.noRoute;
  (void)counters.sendFailed;
  (void)counters.queueFull;
  (void)counters.droppedByFilter;
  (void)counters.blockedBySysEx;

  // MIDI_BASICS: velocity 0 is a note off, and messageKind() says so.
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message noteOnZero;
  espmidi::buildShortMessage(noteOnZero, bytes, 0x90, 60, 0);
  assert(espmidi::messageKind(noteOnZero) == espmidi::KindNoteOff);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_the_guide_still_describes_this_library);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
