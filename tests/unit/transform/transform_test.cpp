// Declarative transformation: value maps, and the rewrites a stage can state as
// data.
//
// Specification: docs/ROUTING.ja.md, and decision 1 in docs/DECISIONS.ja.md for
// why values go through a normalised map rather than being 7-bit integers.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>

namespace
{
int g_ran = 0;

espmidi::Message shortMessage(uint8_t *bytes, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
{
  espmidi::Message message;
  espmidi::buildShortMessage(message, bytes, status, d1, d2);
  return message;
}

void test_normalisation_round_trips()
{
  // A value that goes out and comes back must be unchanged, or every identity
  // transform would drift.
  for (int value = 0; value <= 127; value++)
  {
    const uint8_t original = static_cast<uint8_t>(value);
    assert(espmidi::denormalizeTo7(espmidi::normalizeFrom7(original)) == original);
  }
  assert(espmidi::normalizeFrom7(0) == 0);
  assert(espmidi::normalizeFrom7(127) == 65535);
}

void test_value_map()
{
  // Identity until a range is given, so a Transform with nothing set is
  // transparent.
  const espmidi::ValueMap identity;
  assert(identity.apply7(0) == 0);
  assert(identity.apply7(64) == 64);
  assert(identity.apply7(127) == 127);

  // A volume limiter: the full input onto the lower half.
  const espmidi::ValueMap half = espmidi::ValueMap::scale7(0, 63);
  assert(half.apply7(0) == 0);
  assert(half.apply7(127) == 63);
  assert(half.apply7(64) == 32);

  // A floor: nothing quieter than 40 comes out.
  const espmidi::ValueMap floor = espmidi::ValueMap::scale7(40, 127);
  assert(floor.apply7(0) == 40);
  assert(floor.apply7(127) == 127);

  // One value for everything: a fixed velocity, for a drum pad that should not
  // be touch sensitive.
  const espmidi::ValueMap fixed = espmidi::ValueMap::fixed7(100);
  assert(fixed.apply7(1) == 100);
  assert(fixed.apply7(127) == 100);

  // Reversed output: a pedal wired the wrong way round.
  const espmidi::ValueMap reversed = espmidi::ValueMap::range7(0, 127, 127, 0);
  assert(reversed.apply7(0) == 127);
  assert(reversed.apply7(127) == 0);
  assert(reversed.apply7(64) == 63);

  // Input outside the stated range is clamped rather than extrapolated.
  const espmidi::ValueMap window = espmidi::ValueMap::range7(32, 96, 0, 127);
  assert(window.apply7(0) == 0);
  assert(window.apply7(32) == 0);
  assert(window.apply7(96) == 127);
  assert(window.apply7(127) == 127);
}

void test_channel()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  espmidi::Transform toChannel3;
  toChannel3.channel = 2;
  espmidi::Message message = shortMessage(bytes, 0x90, 60, 100);
  assert(toChannel3.apply(message));
  assert(message.status == 0x92);
  assert(message.channel() == 2);

  // An offset wraps within the sixteen channels rather than overflowing into
  // the command nibble.
  espmidi::Transform offset;
  offset.channelOffset = 5;
  message = shortMessage(bytes, 0x9e, 60, 100);
  assert(offset.apply(message));
  assert(message.status == 0x93); // 14 + 5 = 19, wrapped to 3
  assert(message.command() == 0x90);

  // System messages have no channel and are left alone.
  espmidi::Message clock = shortMessage(bytes, 0xf8);
  assert(toChannel3.apply(clock));
  assert(clock.status == 0xf8);
}

void test_transpose()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Transform octaveUp;
  octaveUp.transpose = 12;

  espmidi::Message message = shortMessage(bytes, 0x90, 60, 100);
  assert(octaveUp.apply(message));
  assert(message.data1 == 72);

  // The release has to move with the note, or it hangs.
  message = shortMessage(bytes, 0x80, 60, 0);
  assert(octaveUp.apply(message));
  assert(message.data1 == 72);

  // Polyphonic pressure is addressed by note number too.
  message = shortMessage(bytes, 0xa0, 60, 50);
  assert(octaveUp.apply(message));
  assert(message.data1 == 72);

  // A note pushed off the keyboard is dropped, not wrapped: wrapping would put
  // it at the wrong end.
  message = shortMessage(bytes, 0x90, 120, 100);
  assert(!octaveUp.apply(message));

  espmidi::Transform down;
  down.transpose = -12;
  message = shortMessage(bytes, 0x90, 5, 100);
  assert(!down.apply(message));

  // transpose and noteOffset compose, so "this device is an octave down" and
  // "this route transposes" can both be stated.
  espmidi::Transform both;
  both.transpose = 12;
  both.noteOffset = -12;
  message = shortMessage(bytes, 0x90, 60, 100);
  assert(both.apply(message));
  assert(message.data1 == 60);

  // Nothing without a note number is touched.
  message = shortMessage(bytes, 0xb0, 7, 64);
  assert(octaveUp.apply(message));
  assert(message.data1 == 7);
}

void test_velocity()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Transform quieter;
  quieter.velocity = espmidi::ValueMap::scale7(0, 63);

  espmidi::Message message = shortMessage(bytes, 0x90, 60, 127);
  assert(quieter.apply(message));
  assert(message.data2 == 63);

  // A note on with velocity 0 is a note off. Scaling it would turn a release
  // into a very quiet note that never stops.
  message = shortMessage(bytes, 0x90, 60, 0);
  assert(quieter.apply(message));
  assert(message.data2 == 0);

  // Release velocity on a real note off is scaled like any other value.
  message = shortMessage(bytes, 0x80, 60, 127);
  assert(quieter.apply(message));
  assert(message.data2 == 63);
}

void test_controller()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  // "CC 7 becomes CC 11" is a filter that narrows to CC 7 plus a transform that
  // sets the number, which is why there is no remapping table.
  espmidi::Transform toExpression;
  toExpression.controller = 11;
  espmidi::Message message = shortMessage(bytes, 0xb0, 7, 64);
  assert(toExpression.apply(message));
  assert(message.data1 == 11);
  assert(message.data2 == 64);

  espmidi::Transform scaled;
  scaled.controllerValue = espmidi::ValueMap::scale7(0, 100);
  message = shortMessage(bytes, 0xb0, 7, 127);
  assert(scaled.apply(message));
  assert(message.data2 == 100);
  assert(message.data1 == 7); // the number is kept when it is not set
}

void test_pressure()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Transform softer;
  softer.pressure = espmidi::ValueMap::scale7(0, 63);

  // Polyphonic pressure carries the value in the second byte.
  espmidi::Message message = shortMessage(bytes, 0xa0, 60, 127);
  assert(softer.apply(message));
  assert(message.data1 == 60);
  assert(message.data2 == 63);

  // Channel pressure carries it in the first, which is the trap this checks.
  message = shortMessage(bytes, 0xd0, 127);
  assert(softer.apply(message));
  assert(message.data1 == 63);
}

void test_data_streams_and_system_messages_are_untouched()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Transform everything;
  everything.channel = 3;
  everything.transpose = 12;
  everything.velocity = espmidi::ValueMap::fixed7(1);

  espmidi::Message clock = shortMessage(bytes, 0xf8);
  assert(everything.apply(clock));
  assert(clock.status == 0xf8);

  espmidi::Message songPosition = shortMessage(bytes, 0xf2, 1, 2);
  assert(everything.apply(songPosition));
  assert(songPosition.status == 0xf2 && songPosition.data1 == 1 && songPosition.data2 == 2);

  espmidi::Message chunk;
  chunk.chunk = true;
  chunk.status = 0xf0;
  assert(everything.apply(chunk));
  assert(chunk.status == 0xf0);
}

// --- Through the router ---------------------------------------------------

struct Sink
{
  static constexpr size_t Capacity = 16;
  uint8_t status[Capacity] = {};
  uint8_t data1[Capacity] = {};
  uint8_t data2[Capacity] = {};
  size_t count = 0;

  static bool write(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    assert(self->count < Capacity);
    self->status[self->count] = message.status;
    self->data1[self->count] = message.data1;
    self->data2[self->count] = message.data2;
    self->count++;
    return true;
  }
};

espmidi::EndpointIdentity uart(uint8_t index)
{
  espmidi::EndpointIdentity identity;
  identity.transport = espmidi::Transport::Uart;
  identity.index = index;
  return identity;
}

struct Fixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  espmidi::InPort in;
  espmidi::OutPort out;
  Sink sink;

  Fixture()
  {
    const espmidi::EndpointId a = registry.attachEndpoint(uart(1), "in");
    const espmidi::EndpointId b = registry.attachEndpoint(uart(2), "out");
    in = registry.attachInPort(a, 0);
    out = registry.attachOutPort(b, 0);
    router.setOutputSink(out, &Sink::write, &sink);
  }

  void receive(uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
  {
    uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
    espmidi::Message message = shortMessage(bytes, status, d1, d2);
    message.port = in.port;
    router.receive(message);
  }
};

void test_cc7_becomes_cc11_on_one_route()
{
  // Filter and transform composing: this is the shape every remapping takes.
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.in, f.out);

  espmidi::Filter onlyVolume;
  onlyVolume.kinds = espmidi::KindControlChange;
  onlyVolume.ccMin = 7;
  onlyVolume.ccMax = 7;
  f.router.setRouteFilter(route, onlyVolume);

  espmidi::Transform toExpression;
  toExpression.controller = 11;
  f.router.setRouteTransform(route, toExpression);

  f.receive(0xb0, 7, 64);  // remapped
  f.receive(0xb0, 1, 64);  // a different controller: filtered out
  f.receive(0x90, 60, 100); // not a controller at all: filtered out
  f.router.update();

  assert(f.sink.count == 1);
  assert(f.sink.status[0] == 0xb0);
  assert(f.sink.data1[0] == 11);
  assert(f.sink.data2[0] == 64);
}

void test_stages_compose_in_order()
{
  // Input stage, route stage and output stage all rewrite the same message, in
  // that order.
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.in, f.out);

  espmidi::Transform inputStage;
  inputStage.noteOffset = 12; // this device is an octave down
  f.router.setInPortTransform(f.in, inputStage);

  espmidi::Transform routeStage;
  routeStage.transpose = 1; // this route transposes a semitone
  f.router.setRouteTransform(route, routeStage);

  espmidi::Transform outputStage;
  outputStage.channel = 5; // this module listens on channel 6
  f.router.setOutPortTransform(f.out, outputStage);

  f.receive(0x90, 60, 100);
  f.router.update();

  assert(f.sink.count == 1);
  assert(f.sink.data1[0] == 73); // 60 + 12 + 1
  assert(f.sink.status[0] == 0x95);
}

void test_a_transform_that_cannot_represent_the_result_drops()
{
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.in, f.out);
  espmidi::Transform up;
  up.transpose = 24;
  f.router.setRouteTransform(route, up);

  f.receive(0x90, 60, 100);  // fine
  f.receive(0x90, 120, 100); // off the keyboard
  f.router.update();

  assert(f.sink.count == 1);
  assert(f.sink.data1[0] == 84);
  assert(f.router.counters().droppedByStage == 1);
}

void test_keyboard_split()
{
  // Two routes from one input, each with its own range and its own channel:
  // the split every stage keyboard has.
  Fixture f;
  const espmidi::EndpointId c = f.registry.attachEndpoint(uart(3), "bass");
  const espmidi::OutPort bass = f.registry.attachOutPort(c, 0);
  Sink bassSink;
  f.router.setOutputSink(bass, &Sink::write, &bassSink);

  const espmidi::Route lower = f.router.addRoute(f.in, bass);
  espmidi::Filter lowerHalf;
  lowerHalf.noteMax = 59;
  f.router.setRouteFilter(lower, lowerHalf);
  espmidi::Transform toBass;
  toBass.channel = 1;
  f.router.setRouteTransform(lower, toBass);

  const espmidi::Route upper = f.router.addRoute(f.in, f.out);
  espmidi::Filter upperHalf;
  upperHalf.noteMin = 60;
  f.router.setRouteFilter(upper, upperHalf);

  f.receive(0x90, 48, 100);
  f.receive(0x90, 72, 100);
  f.router.update();

  assert(bassSink.count == 1);
  assert(bassSink.data1[0] == 48);
  assert(bassSink.status[0] == 0x91);

  assert(f.sink.count == 1);
  assert(f.sink.data1[0] == 72);
  assert(f.sink.status[0] == 0x90);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_normalisation_round_trips);
  run(test_value_map);
  run(test_channel);
  run(test_transpose);
  run(test_velocity);
  run(test_controller);
  run(test_pressure);
  run(test_data_streams_and_system_messages_are_untouched);
  run(test_cc7_becomes_cc11_on_one_route);
  run(test_stages_compose_in_order);
  run(test_a_transform_that_cannot_represent_the_result_drops);
  run(test_keyboard_split);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
