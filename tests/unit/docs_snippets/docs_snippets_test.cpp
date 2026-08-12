// Every API the documents name, compiled.
//
// docs/GUIDE, MIDI_BASICS, RECIPES and API all show code. A rename in the library
// would leave those snippets quietly wrong, and a document that names a method
// which no longer exists is worse than no document at all: the reader trusts it.
//
// So this file uses each of them once. It is not testing behaviour — the subjects
// next door do that — it is testing that **the documents still describe this
// library**. If it stops compiling, a document needs updating.
//
// Only the transport-independent snippets are here. The ones that create a port
// (espmidi::UartPort and friends) are covered by examples_compile, which builds
// the sketches the documents point at.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>

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

// --- RECIPES ------------------------------------------------------------

espmidi::Verdict onlyLoudNotes(void *, espmidi::Message &message)
{
  if ((espmidi::messageKind(message) & espmidi::KindNoteOn) != 0 && message.data2 < 40)
  {
    return espmidi::Verdict::Drop;
  }
  return espmidi::Verdict::Pass;
}

void setLevel(void *, uint8_t, const espmidi::Message &) {}

void test_the_recipes_still_compile()
{
  espmidi::PortRegistry localRegistry;
  espmidi::Router router{localRegistry};
  espmidi::AppPort sketch(router, "sketch");
  espmidi::AppPort monitor(router, "monitor", 1);

  espmidi::EndpointIdentity identity;
  identity.transport = espmidi::Transport::Uart;
  const espmidi::EndpointId endpoint = localRegistry.attachEndpoint(identity, "din");
  const espmidi::InPort in = localRegistry.attachInPort(endpoint, 0);
  const espmidi::OutPort out = localRegistry.attachOutPort(endpoint, 0);

  espmidi::EndpointIdentity second;
  second.transport = espmidi::Transport::Uart;
  second.index = 1;
  const espmidi::EndpointId other = localRegistry.attachEndpoint(second, "synth");
  const espmidi::OutPort synth = localRegistry.attachOutPort(other, 0);

  // "MIDI Thru": deliberately back to the same endpoint.
  const espmidi::Route thru = router.addRoute(in, out);
  assert(router.setRouteAllowSameEndpoint(thru, true));

  // Merging, fanning out, and a group.
  router.addRoute(espmidi::InGroup::all(), synth);
  const espmidi::OutGroup synths = localRegistry.addOutGroup("synths");
  assert(localRegistry.addToGroup(synths, synth));
  const espmidi::Route toGroup = router.addRoute(in, synths);
  assert(router.setRouteEnabled(toGroup, false));

  // Splitting a keyboard.
  espmidi::Filter lower;
  lower.kinds = espmidi::KindNotes;
  lower.noteMin = 36;
  lower.noteMax = 59;
  assert(router.setRouteFilter(toGroup, lower));

  // Transposing, remapping a channel, remapping a controller.
  espmidi::Transform down;
  down.transpose = -12;
  espmidi::Transform toChannel3;
  toChannel3.channel = 2;
  espmidi::Transform toExpression;
  toExpression.controller = 11;
  assert(router.setRouteTransform(toGroup, down));
  assert(router.setRouteTransform(toGroup, toChannel3));
  assert(router.setRouteTransform(toGroup, toExpression));

  espmidi::Filter drumsOnly;
  drumsOnly.allowOnlyChannel(9);
  espmidi::Filter onlyVolume;
  onlyVolume.kinds = espmidi::KindControlChange;
  onlyVolume.ccMin = onlyVolume.ccMax = 7;
  assert(router.setRouteFilter(toGroup, onlyVolume));

  // Value maps: limiting, fixing and reversing.
  espmidi::Transform quieter;
  quieter.velocity = espmidi::ValueMap::scale7(0, 100);
  espmidi::Transform flat;
  flat.velocity = espmidi::ValueMap::fixed7(100);
  espmidi::Transform reversed;
  reversed.controllerValue = espmidi::ValueMap::range7(0, 127, 127, 0);
  assert(router.setRouteTransform(toGroup, reversed));

  // Dropping the clock, and dropping data streams.
  espmidi::Filter quiet;
  quiet.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindSystemRealTime);
  assert(router.setOutPortFilter(monitor.out(), quiet));
  espmidi::Filter noData;
  noData.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindData);

  // "This device is always an octave down", on the input port.
  espmidi::Transform deviceIsLow;
  deviceIsLow.noteOffset = 12;
  assert(router.setInPortTransform(in, deviceIsLow));

  // Deciding in code.
  assert(router.setRouteCallback(toGroup, onlyLoudNotes));

  // An LED following a note, attached to an application port.
  espmidi::Filter note60;
  note60.kinds = espmidi::KindNotes;
  note60.noteMin = note60.noteMax = 60;
  static espmidi::ControlOutput lamp(note60, setLevel);
  monitor.onMessage(&espmidi::ControlOutput::receive, &lamp);

  // Clock: generating, and regenerating a measured one.
  espmidi::ClockGenerator clock(sketch);
  clock.setTempo(12000);
  clock.start(0);
  clock.update(21000);
  espmidi::ClockCounter counter;
  if (counter.microsPerTick() != 0)
  {
    clock.setMicrosPerTick(counter.microsPerTick());
  }
  (void)counter.onQuarter();

  // Sending a dump: no framing bytes in the payload.
  const uint8_t payload[] = {0x7d, 0x01, 0x02};
  espmidi::Message dump;
  dump.type = espmidi::MessageType::Data7;
  dump.status = 0xf0;
  dump.chunk = true;
  dump.chunkStart = true;
  dump.chunkEnd = true;
  dump.chunkData = payload;
  dump.chunkLength = sizeof(payload);
  assert(sketch.send(dump));

  // Checking before sending, and All Notes Off.
  (void)localRegistry.portAvailable(synth.port);
  for (uint8_t channel = 0; channel < 16; channel++)
  {
    sketch.sendShort(static_cast<uint8_t>(0xb0 | channel), 123, 0);
  }
  router.update();

  // Naming a specific device.
  espmidi::PortInfo info;
  if (localRegistry.portInfo(in.port, info) && std::strcmp(info.name, "din") == 0)
  {
    router.addRoute(espmidi::InPort{in.port}, synth);
  }
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
  run(test_the_recipes_still_compile);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
