// The MIDI 1.0 byte stream parser: running status, real-time interleaving and
// SysEx chunking.
//
// Specification: docs/DATA_MODEL.ja.md and docs/ROUTING.ja.md.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
int g_ran = 0;

// Captures what the parser emits. The chunk payload is copied because the
// pointers are only valid during the callback — the rule the whole zero-copy
// SysEx path depends on, so the collector honours it rather than working around
// it.
struct Captured
{
  espmidi::PortId port;
  espmidi::MessageType type;
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  uint8_t dataLength;
  uint8_t raw[espmidi::MaxShortMessageBytes];
  size_t length;
  bool chunk;
  bool chunkStart;
  bool chunkEnd;
  uint8_t chunkData[64];
  size_t chunkLength;
};

struct Collector
{
  static constexpr size_t Capacity = 32;
  Captured items[Capacity] = {};
  size_t count = 0;

  void operator()(const espmidi::Message &message)
  {
    assert(count < Capacity);
    Captured &out = items[count++];
    out.port = message.port;
    out.type = message.type;
    out.status = message.status;
    out.data1 = message.data1;
    out.data2 = message.data2;
    out.dataLength = message.dataLength;
    out.length = message.length;
    std::memset(out.raw, 0, sizeof(out.raw));
    if (message.raw && message.length <= sizeof(out.raw))
    {
      std::memcpy(out.raw, message.raw, message.length);
    }
    out.chunk = message.chunk;
    out.chunkStart = message.chunkStart;
    out.chunkEnd = message.chunkEnd;
    out.chunkLength = message.chunkLength;
    std::memset(out.chunkData, 0, sizeof(out.chunkData));
    assert(message.chunkLength <= sizeof(out.chunkData));
    if (message.chunkData && message.chunkLength > 0)
    {
      std::memcpy(out.chunkData, message.chunkData, message.chunkLength);
    }
  }

  void clear() { count = 0; }
};

void feed(espmidi::Parser &parser, Collector &collector, const uint8_t *bytes, size_t length)
{
  parser.parse(bytes, length, [&collector](const espmidi::Message &message) { collector(message); });
}

void test_channel_voice_message()
{
  espmidi::Parser parser{espmidi::PortId{7}};
  Collector collector;

  const uint8_t bytes[] = {0x90, 60, 100};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 1);
  const Captured &m = collector.items[0];
  assert(m.port == espmidi::PortId{7}); // the parser stamps its port
  assert(m.type == espmidi::MessageType::Midi1ChannelVoice);
  assert(m.status == 0x90);
  assert(m.data1 == 60);
  assert(m.data2 == 100);
  assert(m.dataLength == 2);
  assert(m.length == 3);
  assert(m.raw[0] == 0x90 && m.raw[1] == 60 && m.raw[2] == 100);
  assert(!m.chunk);
}

void test_message_split_across_calls()
{
  // A transport delivers whatever arrived, not whole messages.
  espmidi::Parser parser;
  Collector collector;

  const uint8_t first[] = {0x90};
  const uint8_t second[] = {60};
  const uint8_t third[] = {100};
  feed(parser, collector, first, sizeof(first));
  assert(collector.count == 0);
  feed(parser, collector, second, sizeof(second));
  assert(collector.count == 0);
  feed(parser, collector, third, sizeof(third));

  assert(collector.count == 1);
  assert(collector.items[0].status == 0x90);
  assert(collector.items[0].data2 == 100);
}

void test_running_status()
{
  espmidi::Parser parser;
  Collector collector;

  // Three notes, one status byte. The consumer sees a resolved status on all of
  // them, which is the point of resolving it here rather than downstream.
  const uint8_t bytes[] = {0x90, 60, 100, 62, 101, 64, 102};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 3);
  for (size_t i = 0; i < collector.count; i++)
  {
    assert(collector.items[i].status == 0x90);
    assert(collector.items[i].dataLength == 2);
  }
  assert(collector.items[0].data1 == 60);
  assert(collector.items[1].data1 == 62);
  assert(collector.items[2].data1 == 64);
}

void test_running_status_with_one_data_byte()
{
  espmidi::Parser parser;
  Collector collector;

  const uint8_t bytes[] = {0xc0, 1, 2, 3};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 3);
  for (size_t i = 0; i < collector.count; i++)
  {
    assert(collector.items[i].status == 0xc0);
    assert(collector.items[i].dataLength == 1);
    assert(collector.items[i].length == 2);
  }
  assert(collector.items[2].data1 == 3);
}

void test_system_common_cancels_running_status()
{
  espmidi::Parser parser;
  Collector collector;

  // Song Select in the middle: the data bytes after it belong to nothing, so
  // they are dropped rather than being read as another note.
  const uint8_t bytes[] = {0x90, 60, 100, 0xf3, 5, 62, 101};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 2);
  assert(collector.items[0].status == 0x90);
  assert(collector.items[1].status == 0xf3);
  assert(collector.items[1].data1 == 5);
  assert(collector.items[1].dataLength == 1);
}

void test_real_time_does_not_disturb_running_status()
{
  espmidi::Parser parser;
  Collector collector;

  // Clock between the data bytes of a note, and again where a running-status
  // note would start. Neither may break the note or cancel running status.
  const uint8_t bytes[] = {0x90, 60, 0xf8, 100, 0xf8, 62, 101};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 4);
  assert(collector.items[0].status == 0xf8); // clock arrives first, mid-note
  assert(collector.items[0].dataLength == 0);
  assert(collector.items[1].status == 0x90); // the note completes afterwards
  assert(collector.items[1].data1 == 60);
  assert(collector.items[1].data2 == 100);
  assert(collector.items[2].status == 0xf8);
  assert(collector.items[3].status == 0x90); // running status survived
  assert(collector.items[3].data1 == 62);
}

void test_tune_request_needs_no_data()
{
  espmidi::Parser parser;
  Collector collector;

  const uint8_t bytes[] = {0xf6};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 1);
  assert(collector.items[0].status == 0xf6);
  assert(collector.items[0].dataLength == 0);
  assert(collector.items[0].length == 1);
}

void test_undefined_status_is_ignored()
{
  espmidi::Parser parser;
  Collector collector;

  // 0xF4 and 0xF5 are undefined and a receiver must ignore them. They are
  // System Common, so they also cancel running status.
  const uint8_t bytes[] = {0x90, 60, 100, 0xf4, 62, 101};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 1);
  assert(collector.items[0].status == 0x90);
}

void test_orphan_data_bytes_are_dropped()
{
  espmidi::Parser parser;
  Collector collector;

  const uint8_t bytes[] = {60, 100, 62};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 0);
}

void test_sysex_in_one_call()
{
  espmidi::Parser parser{espmidi::PortId{2}};
  Collector collector;

  const uint8_t bytes[] = {0xf0, 0x41, 0x10, 0x42, 0xf7};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 1);
  const Captured &m = collector.items[0];
  assert(m.chunk);
  assert(m.chunkStart);
  assert(m.chunkEnd);
  assert(m.type == espmidi::MessageType::Data7);
  assert(m.port == espmidi::PortId{2});
  // The framing bytes are not payload.
  assert(m.chunkLength == 3);
  assert(m.chunkData[0] == 0x41 && m.chunkData[1] == 0x10 && m.chunkData[2] == 0x42);
  assert(!parser.inSysEx());
}

void test_empty_sysex()
{
  espmidi::Parser parser;
  Collector collector;

  const uint8_t bytes[] = {0xf0, 0xf7};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 1);
  assert(collector.items[0].chunk);
  assert(collector.items[0].chunkStart);
  assert(collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 0);
}

void test_sysex_split_across_calls()
{
  // A patch dump arrives in whatever pieces the transport delivers. Only the
  // first chunk carries chunkStart and only the last carries chunkEnd, so a
  // downstream port sees one stream rather than several.
  espmidi::Parser parser;
  Collector collector;

  const uint8_t first[] = {0xf0, 0x41, 0x10};
  const uint8_t second[] = {0x42, 0x43};
  const uint8_t third[] = {0x44, 0xf7};

  feed(parser, collector, first, sizeof(first));
  assert(collector.count == 1);
  assert(collector.items[0].chunkStart);
  assert(!collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 2);
  assert(parser.inSysEx());

  feed(parser, collector, second, sizeof(second));
  assert(collector.count == 2);
  assert(!collector.items[1].chunkStart);
  assert(!collector.items[1].chunkEnd);
  assert(collector.items[1].chunkLength == 2);

  feed(parser, collector, third, sizeof(third));
  assert(collector.count == 3);
  assert(!collector.items[2].chunkStart);
  assert(collector.items[2].chunkEnd);
  assert(collector.items[2].chunkLength == 1);
  assert(collector.items[2].chunkData[0] == 0x44);
  assert(!parser.inSysEx());
}

void test_sysex_start_at_end_of_buffer()
{
  // The 0xF0 lands alone at the end of a transfer. Nothing is emitted yet, but
  // the chunk that follows must still be marked as the start of the stream.
  espmidi::Parser parser;
  Collector collector;

  const uint8_t first[] = {0xf0};
  feed(parser, collector, first, sizeof(first));
  assert(collector.count == 0);
  assert(parser.inSysEx());

  const uint8_t second[] = {0x41, 0xf7};
  feed(parser, collector, second, sizeof(second));
  assert(collector.count == 1);
  assert(collector.items[0].chunkStart);
  assert(collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 1);
}

void test_real_time_inside_sysex()
{
  // Clock is allowed inside a dump. It is delivered as its own message and it
  // splits the chunk, because a chunk has to be contiguous in memory to be
  // handed over without copying — but it does not end the stream.
  espmidi::Parser parser;
  Collector collector;

  const uint8_t bytes[] = {0xf0, 0x41, 0xf8, 0x42, 0xf7};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 3);
  assert(collector.items[0].chunk);
  assert(collector.items[0].chunkStart);
  assert(!collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 1);
  assert(collector.items[0].chunkData[0] == 0x41);

  assert(!collector.items[1].chunk);
  assert(collector.items[1].status == 0xf8);

  assert(collector.items[2].chunk);
  assert(!collector.items[2].chunkStart);
  assert(collector.items[2].chunkEnd);
  assert(collector.items[2].chunkLength == 1);
  assert(collector.items[2].chunkData[0] == 0x42);
}

void test_status_byte_terminates_sysex()
{
  // A device that abandons a dump and starts playing leaves a truncated stream.
  // The chunk is closed so a downstream port can finish what it started rather
  // than wait for an 0xF7 that never comes (docs/ROUTING.ja.md, rule 2).
  espmidi::Parser parser;
  Collector collector;

  const uint8_t bytes[] = {0xf0, 0x41, 0x42, 0x90, 60, 100};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 2);
  assert(collector.items[0].chunk);
  assert(collector.items[0].chunkStart);
  assert(collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 2);

  assert(!collector.items[1].chunk);
  assert(collector.items[1].status == 0x90);
  assert(collector.items[1].data1 == 60);
  assert(!parser.inSysEx());
}

void test_stray_sysex_end_is_ignored()
{
  espmidi::Parser parser;
  Collector collector;

  // An 0xF7 with no stream open emits nothing. It is System Common, so it also
  // cancels running status: the note bytes after it have no owner.
  const uint8_t bytes[] = {0x90, 60, 100, 0xf7, 62, 101};
  feed(parser, collector, bytes, sizeof(bytes));

  assert(collector.count == 1);
  assert(collector.items[0].status == 0x90);
}

void test_reset_drops_partial_state()
{
  // A port calls reset() when its link goes away, so bytes from before a
  // disconnect cannot combine with bytes from after it.
  espmidi::Parser parser;
  Collector collector;

  const uint8_t partial[] = {0x90, 60};
  feed(parser, collector, partial, sizeof(partial));
  assert(collector.count == 0);

  parser.reset();

  const uint8_t rest[] = {100};
  feed(parser, collector, rest, sizeof(rest));
  assert(collector.count == 0); // no half message completed by unrelated bytes

  const uint8_t sysex[] = {0xf0, 0x41};
  feed(parser, collector, sysex, sizeof(sysex));
  assert(parser.inSysEx());
  collector.clear();
  parser.reset();
  assert(!parser.inSysEx());

  // After a reset the stream is gone, so a continuation is not attached to it.
  const uint8_t after[] = {0x42, 0xf7};
  feed(parser, collector, after, sizeof(after));
  assert(collector.count == 0);
}

void test_set_port()
{
  espmidi::Parser parser;
  Collector collector;
  assert(!parser.port().valid());

  parser.setPort(espmidi::PortId{11});
  assert(parser.port() == espmidi::PortId{11});

  const uint8_t bytes[] = {0xb0, 7, 64};
  feed(parser, collector, bytes, sizeof(bytes));
  assert(collector.count == 1);
  assert(collector.items[0].port == espmidi::PortId{11});
}

void test_null_input_is_safe()
{
  espmidi::Parser parser;
  Collector collector;
  feed(parser, collector, nullptr, 3);
  assert(collector.count == 0);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_channel_voice_message);
  run(test_message_split_across_calls);
  run(test_running_status);
  run(test_running_status_with_one_data_byte);
  run(test_system_common_cancels_running_status);
  run(test_real_time_does_not_disturb_running_status);
  run(test_tune_request_needs_no_data);
  run(test_undefined_status_is_ignored);
  run(test_orphan_data_bytes_are_dropped);
  run(test_sysex_in_one_call);
  run(test_empty_sysex);
  run(test_sysex_split_across_calls);
  run(test_sysex_start_at_end_of_buffer);
  run(test_real_time_inside_sysex);
  run(test_status_byte_terminates_sysex);
  run(test_stray_sysex_end_is_ignored);
  run(test_reset_drops_partial_state);
  run(test_set_port);
  run(test_null_input_is_safe);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
