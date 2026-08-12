// Declarative filtering: what a message is, and which ones a stage lets past.
//
// Specification: docs/ROUTING.ja.md.

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

void test_message_kinds()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  assert(espmidi::messageKind(shortMessage(bytes, 0x80, 60, 0)) == espmidi::KindNoteOff);
  assert(espmidi::messageKind(shortMessage(bytes, 0x90, 60, 100)) == espmidi::KindNoteOn);
  assert(espmidi::messageKind(shortMessage(bytes, 0xa0, 60, 50)) == espmidi::KindPolyPressure);
  assert(espmidi::messageKind(shortMessage(bytes, 0xb0, 7, 64)) == espmidi::KindControlChange);
  assert(espmidi::messageKind(shortMessage(bytes, 0xc0, 5)) == espmidi::KindProgramChange);
  assert(espmidi::messageKind(shortMessage(bytes, 0xd0, 40)) == espmidi::KindChannelPressure);
  assert(espmidi::messageKind(shortMessage(bytes, 0xe0, 0, 64)) == espmidi::KindPitchBend);
  assert(espmidi::messageKind(shortMessage(bytes, 0xf2, 1, 2)) == espmidi::KindSystemCommon);
  assert(espmidi::messageKind(shortMessage(bytes, 0xf8)) == espmidi::KindSystemRealTime);

  // A note on with velocity 0 is a note off on the wire. Reporting it as a note
  // on would let "block note off" leak every release through.
  assert(espmidi::messageKind(shortMessage(bytes, 0x90, 60, 0)) == espmidi::KindNoteOff);

  espmidi::Message chunk;
  chunk.chunk = true;
  chunk.status = 0xf0;
  assert(espmidi::messageKind(chunk) == espmidi::KindData);
}

void test_default_filter_passes_everything()
{
  const espmidi::Filter filter;
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  assert(filter.accepts(shortMessage(bytes, 0x90, 0, 1)));
  assert(filter.accepts(shortMessage(bytes, 0x9f, 127, 127)));
  assert(filter.accepts(shortMessage(bytes, 0xf8)));
  assert(filter.accepts(shortMessage(bytes, 0xb0, 0, 0)));

  espmidi::Message chunk;
  chunk.chunk = true;
  chunk.status = 0xf0;
  assert(filter.accepts(chunk));
}

void test_kinds()
{
  espmidi::Filter filter;
  filter.kinds = espmidi::KindNotes;
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  assert(filter.accepts(shortMessage(bytes, 0x90, 60, 100)));
  assert(filter.accepts(shortMessage(bytes, 0x80, 60, 0)));
  assert(!filter.accepts(shortMessage(bytes, 0xb0, 7, 64)));
  assert(!filter.accepts(shortMessage(bytes, 0xf8)));

  // "Everything but the clock" is a common monitor setting.
  espmidi::Filter noClock;
  noClock.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindSystemRealTime);
  assert(!noClock.accepts(shortMessage(bytes, 0xf8)));
  assert(noClock.accepts(shortMessage(bytes, 0x90, 60, 100)));

  // Blocking data streams is how a route says "no patch dumps here".
  espmidi::Filter noData;
  noData.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindData);
  espmidi::Message chunk;
  chunk.chunk = true;
  chunk.status = 0xf0;
  assert(!noData.accepts(chunk));
}

void test_channels()
{
  espmidi::Filter filter;
  filter.allowOnlyChannel(0);
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  assert(filter.accepts(shortMessage(bytes, 0x90, 60, 100)));
  assert(!filter.accepts(shortMessage(bytes, 0x91, 60, 100)));

  filter.allowChannel(9); // the drum channel as well
  assert(filter.accepts(shortMessage(bytes, 0x99, 60, 100)));
  assert(!filter.accepts(shortMessage(bytes, 0x92, 60, 100)));

  filter.blockChannel(0);
  assert(!filter.accepts(shortMessage(bytes, 0x90, 60, 100)));
  assert(filter.accepts(shortMessage(bytes, 0x99, 60, 100)));

  // System messages have no channel, so a channel filter never blocks them.
  espmidi::Filter oneChannel;
  oneChannel.allowOnlyChannel(3);
  assert(oneChannel.accepts(shortMessage(bytes, 0xf8)));
  assert(oneChannel.accepts(shortMessage(bytes, 0xf2, 1, 2)));
}

void test_note_range()
{
  // Splitting a keyboard: the lower half to a bass sound.
  espmidi::Filter filter;
  filter.noteMin = 36;
  filter.noteMax = 59;
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  assert(filter.accepts(shortMessage(bytes, 0x90, 36, 100)));
  assert(filter.accepts(shortMessage(bytes, 0x90, 59, 100)));
  assert(!filter.accepts(shortMessage(bytes, 0x90, 60, 100)));
  assert(!filter.accepts(shortMessage(bytes, 0x90, 35, 100)));

  // The release of a note has to obey the same range, or notes hang.
  assert(filter.accepts(shortMessage(bytes, 0x80, 40, 0)));
  assert(!filter.accepts(shortMessage(bytes, 0x80, 70, 0)));

  // Polyphonic pressure is per note and follows the range too.
  assert(filter.accepts(shortMessage(bytes, 0xa0, 40, 50)));
  assert(!filter.accepts(shortMessage(bytes, 0xa0, 70, 50)));

  // Messages with no note are unaffected.
  assert(filter.accepts(shortMessage(bytes, 0xb0, 7, 64)));
  assert(filter.accepts(shortMessage(bytes, 0xc0, 5)));
}

void test_controller_range()
{
  espmidi::Filter filter;
  filter.ccMin = 7;
  filter.ccMax = 7;
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};

  assert(filter.accepts(shortMessage(bytes, 0xb0, 7, 64)));
  assert(!filter.accepts(shortMessage(bytes, 0xb0, 11, 64)));
  // The note range and the controller range do not interfere.
  assert(filter.accepts(shortMessage(bytes, 0x90, 60, 100)));
}

// --- Through the router ---------------------------------------------------

struct Sink
{
  static constexpr size_t Capacity = 16;
  uint8_t status[Capacity] = {};
  uint8_t data1[Capacity] = {};
  size_t count = 0;

  static bool write(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    assert(self->count < Capacity);
    self->status[self->count] = message.status;
    self->data1[self->count] = message.data1;
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

void test_route_filter_narrows()
{
  // Send only channel 10 to the drum module.
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.in, f.out);
  espmidi::Filter filter;
  filter.allowOnlyChannel(9);
  f.router.setRouteFilter(route, filter);

  f.receive(0x99, 38, 100);
  f.receive(0x90, 60, 100);
  f.router.update();

  assert(f.sink.count == 1);
  assert(f.sink.status[0] == 0x99);
  assert(f.router.counters().droppedByFilter == 1);
}

void test_port_filters_apply_at_both_ends()
{
  Fixture f;
  f.router.addRoute(f.in, f.out);

  // "This device only sends useful things on channel 1."
  espmidi::Filter inputOnly;
  inputOnly.allowOnlyChannel(0);
  f.router.setInPortFilter(f.in, inputOnly);

  // "This module cannot deal with pitch bend."
  espmidi::Filter outputOnly;
  outputOnly.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindPitchBend);
  f.router.setOutPortFilter(f.out, outputOnly);

  f.receive(0x91, 60, 100); // wrong channel, stopped at the input
  f.receive(0xe0, 0, 64);   // pitch bend, stopped at the output
  f.receive(0x90, 60, 100); // gets through
  f.router.update();

  assert(f.sink.count == 1);
  assert(f.sink.status[0] == 0x90);
  assert(f.router.counters().droppedByFilter == 2);
}

void test_a_filter_can_block_a_stream_at_its_start()
{
  // Rule 1 says a stream's path is fixed when it starts, so a filter is the one
  // rule that a chunk meets — and only on the first chunk.
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.in, f.out);
  espmidi::Filter noData;
  noData.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindData);
  f.router.setRouteFilter(route, noData);

  const uint8_t payload[] = {0x41};
  espmidi::Message chunk;
  chunk.port = f.in.port;
  chunk.type = espmidi::MessageType::Data7;
  chunk.status = 0xf0;
  chunk.chunk = true;
  chunk.chunkStart = true;
  chunk.chunkData = payload;
  chunk.chunkLength = sizeof(payload);
  f.router.receive(chunk);

  chunk.chunkStart = false;
  chunk.chunkEnd = true;
  f.router.receive(chunk);
  f.router.update();

  assert(f.sink.count == 0);
  // The output never became busy, so a later stream is not stuck behind a
  // stream that was refused.
  assert(!f.router.outputBusy(f.out));

  // Ordinary messages on the same route are unaffected.
  f.receive(0x90, 60, 100);
  f.router.update();
  assert(f.sink.count == 1);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_message_kinds);
  run(test_default_filter_passes_everything);
  run(test_kinds);
  run(test_channels);
  run(test_note_range);
  run(test_controller_range);
  run(test_route_filter_narrows);
  run(test_port_filters_apply_at_both_ends);
  run(test_a_filter_can_block_a_stream_at_its_start);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
