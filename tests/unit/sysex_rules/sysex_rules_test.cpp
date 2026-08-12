// The three SysEx stream rules, and how a chunk moves through the queue.
//
// Specification: docs/ROUTING.ja.md.
//
//   1. A stream's path is decided when it starts.
//   2. An input that disappears mid-stream has its outputs closed for it.
//   3. An output carries one stream at a time; only System Real-Time interrupts.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
int g_ran = 0;

struct Event
{
  bool chunk = false;
  bool chunkStart = false;
  bool chunkEnd = false;
  uint8_t status = 0;
  uint8_t payload[64] = {};
  size_t payloadLength = 0;
};

struct Sink
{
  static constexpr size_t Capacity = 32;
  Event items[Capacity] = {};
  size_t count = 0;
  bool accept = true;

  static bool write(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    if (!self->accept)
    {
      return false;
    }
    assert(self->count < Capacity);
    Event &event = self->items[self->count++];
    event.chunk = message.chunk;
    event.chunkStart = message.chunkStart;
    event.chunkEnd = message.chunkEnd;
    event.status = message.status;
    event.payloadLength = message.chunkLength;
    assert(message.chunkLength <= sizeof(event.payload));
    if (message.chunkData && message.chunkLength > 0)
    {
      std::memcpy(event.payload, message.chunkData, message.chunkLength);
    }
    return true;
  }

  // Everything the sink received, concatenated, so a split stream can be
  // compared against what went in.
  size_t collect(uint8_t *dst, size_t capacity) const
  {
    size_t offset = 0;
    for (size_t i = 0; i < count; i++)
    {
      assert(offset + items[i].payloadLength <= capacity);
      std::memcpy(&dst[offset], items[i].payload, items[i].payloadLength);
      offset += items[i].payloadLength;
    }
    return offset;
  }

  void clear() { count = 0; }
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
  espmidi::EndpointId endpointA;
  espmidi::EndpointId endpointB;
  espmidi::EndpointId endpointC;
  espmidi::InPort inA;
  espmidi::InPort inC;
  espmidi::OutPort outB;
  espmidi::OutPort outC;
  Sink sinkB;
  Sink sinkC;

  Fixture()
  {
    endpointA = registry.attachEndpoint(uart(1), "source A");
    endpointB = registry.attachEndpoint(uart(2), "target B");
    endpointC = registry.attachEndpoint(uart(3), "other C");
    inA = registry.attachInPort(endpointA, 0);
    inC = registry.attachInPort(endpointC, 0);
    outB = registry.attachOutPort(endpointB, 0);
    outC = registry.attachOutPort(endpointC, 0);
    router.setOutputSink(outB, &Sink::write, &sinkB);
    router.setOutputSink(outC, &Sink::write, &sinkC);
  }

  bool chunk(espmidi::InPort port, const uint8_t *data, size_t length, bool start, bool end)
  {
    espmidi::Message message;
    message.port = port.port;
    message.type = espmidi::MessageType::Data7;
    message.status = 0xf0;
    message.chunk = true;
    message.chunkStart = start;
    message.chunkEnd = end;
    message.chunkData = data;
    message.chunkLength = length;
    return router.receive(message);
  }

  bool shortMessage(espmidi::InPort port, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
  {
    uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
    espmidi::Message message;
    espmidi::buildShortMessage(message, bytes, status, d1, d2);
    message.port = port.port;
    return router.receive(message);
  }
};

void test_a_stream_passes_through_in_order()
{
  Fixture f;
  f.router.addRoute(f.inA, f.outB);

  const uint8_t first[] = {0x41, 0x10};
  const uint8_t second[] = {0x42};
  f.chunk(f.inA, first, sizeof(first), true, false);
  f.chunk(f.inA, second, sizeof(second), false, true);
  f.router.update();

  assert(f.sinkB.count == 2);
  assert(f.sinkB.items[0].chunk && f.sinkB.items[0].chunkStart && !f.sinkB.items[0].chunkEnd);
  assert(f.sinkB.items[1].chunkEnd && !f.sinkB.items[1].chunkStart);

  uint8_t rebuilt[16] = {};
  const size_t length = f.sinkB.collect(rebuilt, sizeof(rebuilt));
  assert(length == 3);
  assert(rebuilt[0] == 0x41 && rebuilt[1] == 0x10 && rebuilt[2] == 0x42);
}

void test_a_large_chunk_is_split_but_stays_one_stream()
{
  // The queue holds a bounded payload per entry, so a big chunk is split. Only
  // the first piece may say "start" and only the last "end", or a downstream
  // port would see several streams instead of one.
  Fixture f;
  f.router.addRoute(f.inA, f.outB);

  uint8_t payload[ESPMIDI_CHUNK_BYTES * 2 + 5];
  for (size_t i = 0; i < sizeof(payload); i++)
  {
    payload[i] = static_cast<uint8_t>(i & 0x7f);
  }
  assert(f.chunk(f.inA, payload, sizeof(payload), true, true));
  f.router.update();

  assert(f.sinkB.count == 3);
  assert(f.sinkB.items[0].chunkStart && !f.sinkB.items[0].chunkEnd);
  assert(!f.sinkB.items[1].chunkStart && !f.sinkB.items[1].chunkEnd);
  assert(!f.sinkB.items[2].chunkStart && f.sinkB.items[2].chunkEnd);

  uint8_t rebuilt[sizeof(payload)] = {};
  const size_t length = f.sinkB.collect(rebuilt, sizeof(rebuilt));
  assert(length == sizeof(payload));
  assert(std::memcmp(rebuilt, payload, sizeof(payload)) == 0);
}

espmidi::Verdict dropAll(void *context, espmidi::Message &message)
{
  (void)message;
  (*static_cast<int *>(context))++;
  return espmidi::Verdict::Drop;
}

void test_chunks_bypass_transforms()
{
  // Interpreting a data stream is not this library's job, and re-deciding a
  // route halfway through one would break rule 1. A stage that drops everything
  // does not touch a stream.
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.inA, f.outB);
  int calls = 0;
  f.router.setRouteTransform(route, &dropAll, &calls);

  const uint8_t payload[] = {0x41};
  f.chunk(f.inA, payload, sizeof(payload), true, true);
  f.router.update();

  assert(calls == 0);
  assert(f.sinkB.count == 1);

  // A short message on the same route is dropped, so the stage really is armed.
  f.shortMessage(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(calls == 1);
  assert(f.sinkB.count == 1);
}

void test_rule1_the_path_is_fixed_when_the_stream_starts()
{
  Fixture f;
  const espmidi::Route toB = f.router.addRoute(f.inA, f.outB);

  const uint8_t first[] = {0x41};
  f.chunk(f.inA, first, sizeof(first), true, false);
  f.router.update();
  assert(f.sinkB.count == 1);

  // Routing changes in the middle of the dump: a new destination appears and
  // the original one is removed. The stream in flight ignores both.
  f.router.removeRoute(toB);
  f.router.addRoute(f.inA, f.outC);

  const uint8_t second[] = {0x42};
  f.chunk(f.inA, second, sizeof(second), false, true);
  f.router.update();

  assert(f.sinkB.count == 2); // finished where it started
  assert(f.sinkB.items[1].chunkEnd);
  assert(f.sinkC.count == 0); // and did not leak into the new route

  // The next stream uses the new routing.
  f.chunk(f.inA, first, sizeof(first), true, true);
  f.router.update();
  assert(f.sinkC.count == 1);
  assert(f.sinkB.count == 2);
}

void test_rule2_disconnect_closes_the_stream()
{
  // The device stops talking mid-dump. The output would otherwise wait forever
  // for an 0xF7, so one is produced on the input's behalf.
  Fixture f;
  f.router.addRoute(f.inA, f.outB);

  const uint8_t payload[] = {0x41, 0x42};
  f.chunk(f.inA, payload, sizeof(payload), true, false);
  f.router.update();
  assert(f.sinkB.count == 1);
  assert(!f.sinkB.items[0].chunkEnd);
  assert(f.router.outputBusy(f.outB));

  f.registry.detachEndpoint(f.endpointA);
  // Nothing is sent from the disconnect itself: that runs in the transport's
  // context. It happens on the next pass.
  assert(f.sinkB.count == 1);

  f.router.update();
  assert(f.sinkB.count == 2);
  assert(f.sinkB.items[1].chunk);
  assert(f.sinkB.items[1].chunkEnd);
  assert(f.sinkB.items[1].payloadLength == 0);
  assert(!f.router.outputBusy(f.outB));

  // Closing happens once, not on every later update.
  f.router.update();
  assert(f.sinkB.count == 2);
}

void test_rule3_one_stream_per_output()
{
  // Two dumps aimed at one output would interleave and corrupt both, so the
  // second is refused for that output rather than mixed in.
  Fixture f;
  f.router.addRoute(f.inA, f.outB);
  f.router.addRoute(f.inC, f.outB);

  const uint8_t a[] = {0x41};
  const uint8_t c[] = {0x51};
  f.chunk(f.inA, a, sizeof(a), true, false);
  f.chunk(f.inC, c, sizeof(c), true, false);
  f.router.update();

  assert(f.sinkB.count == 1); // only the first stream got through
  assert(f.router.counters().sysExRejected == 1);

  // The first stream finishes normally, and then the output is free again.
  f.chunk(f.inA, a, sizeof(a), false, true);
  f.router.update();
  assert(f.sinkB.count == 2);
  assert(!f.router.outputBusy(f.outB));

  f.chunk(f.inC, c, sizeof(c), true, true);
  f.router.update();
  assert(f.sinkB.count == 3);
}

void test_rule3_only_real_time_interrupts_a_stream()
{
  Fixture f;
  f.router.addRoute(f.inA, f.outB);
  f.router.addRoute(f.inC, f.outB);

  const uint8_t payload[] = {0x41};
  f.chunk(f.inA, payload, sizeof(payload), true, false);
  f.router.update();
  assert(f.sinkB.count == 1);

  // MIDI Clock keeps flowing: the specification allows it between an 0xF0 and
  // its 0xF7, and a stopped clock would stop the music.
  f.shortMessage(f.inC, 0xf8);
  f.router.update();
  assert(f.sinkB.count == 2);
  assert(!f.sinkB.items[1].chunk);
  assert(f.sinkB.items[1].status == 0xf8);

  // A note cannot go out mid-stream at all, so it is dropped and counted rather
  // than delayed behind the dump.
  f.shortMessage(f.inC, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 2);
  assert(f.router.counters().blockedBySysEx == 1);

  // Once the stream ends, ordinary messages flow again.
  f.chunk(f.inA, payload, sizeof(payload), false, true);
  f.router.update();
  f.shortMessage(f.inC, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 4);
}

void test_a_stream_can_fan_out_and_each_output_tracks_it()
{
  Fixture f;
  f.router.addRoute(f.inA, f.outB);
  f.router.addRoute(f.inA, f.outC);

  const uint8_t payload[] = {0x41};
  f.chunk(f.inA, payload, sizeof(payload), true, false);
  f.router.update();
  assert(f.sinkB.count == 1 && f.sinkC.count == 1);
  assert(f.router.outputBusy(f.outB) && f.router.outputBusy(f.outC));

  f.chunk(f.inA, payload, sizeof(payload), false, true);
  f.router.update();
  assert(f.sinkB.count == 2 && f.sinkC.count == 2);
  assert(!f.router.outputBusy(f.outB) && !f.router.outputBusy(f.outC));
}

void test_a_continuation_without_a_start_is_dropped()
{
  Fixture f;
  f.router.addRoute(f.inA, f.outB);

  const uint8_t payload[] = {0x41};
  f.chunk(f.inA, payload, sizeof(payload), false, true);
  f.router.update();
  assert(f.sinkB.count == 0);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_a_stream_passes_through_in_order);
  run(test_a_large_chunk_is_split_but_stays_one_stream);
  run(test_chunks_bypass_transforms);
  run(test_rule1_the_path_is_fixed_when_the_stream_starts);
  run(test_rule2_disconnect_closes_the_stream);
  run(test_rule3_one_stream_per_output);
  run(test_rule3_only_real_time_interrupts_a_stream);
  run(test_a_stream_can_fan_out_and_each_output_tracks_it);
  run(test_a_continuation_without_a_start_is_dropped);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
