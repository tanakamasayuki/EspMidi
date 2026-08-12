// espmidi::Message back to a MIDI 1.0 byte stream.
//
// The receiving direction is fixed in unit/parser; this is the other half. The
// two are checked against each other at the end: whatever the parser produces
// from a stream, the serializer has to turn back into the same stream.
//
// Specification: docs/PORTS.ja.md, docs/DATA_MODEL.ja.md.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int g_ran = 0;

// Collects what a serializer writes, and can be told to refuse.
struct Wire
{
  std::vector<uint8_t> bytes;
  size_t refuseAfter = 0; // 0 means never
  size_t writes = 0;

  bool operator()(const uint8_t *data, size_t length)
  {
    writes++;
    if (refuseAfter != 0 && writes > refuseAfter)
    {
      return false;
    }
    bytes.insert(bytes.end(), data, data + length);
    return true;
  }

  bool is(std::initializer_list<uint8_t> expected) const
  {
    return bytes.size() == expected.size() && std::memcmp(bytes.data(), expected.begin(), bytes.size()) == 0;
  }
};

espmidi::Message shortMessage(uint8_t *storage, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
{
  espmidi::Message message;
  espmidi::buildShortMessage(message, storage, status, d1, d2);
  return message;
}

espmidi::Message chunkMessage(const uint8_t *payload, size_t length, bool start, bool end)
{
  espmidi::Message message;
  message.type = espmidi::MessageType::Data7;
  message.status = 0xf0;
  message.chunk = true;
  message.chunkStart = start;
  message.chunkEnd = end;
  message.chunkData = payload;
  message.chunkLength = length;
  return message;
}

void test_short_messages()
{
  espmidi::Serializer serializer;
  uint8_t storage[espmidi::MaxShortMessageBytes] = {};
  Wire wire;

  assert(serializer.serialize(shortMessage(storage, 0x90, 60, 100), wire));
  assert(serializer.serialize(shortMessage(storage, 0xc0, 5), wire));
  assert(serializer.serialize(shortMessage(storage, 0xf8), wire));

  assert(wire.is({0x90, 60, 100, 0xc0, 5, 0xf8}));
}

void test_running_status_is_not_used()
{
  // Three notes in a row could go out as one status byte and six data bytes. They
  // do not: an output port carries messages from several inputs, and a receiver
  // that drops one byte of a compressed stream misreads everything after it.
  espmidi::Serializer serializer;
  uint8_t storage[espmidi::MaxShortMessageBytes] = {};
  Wire wire;

  serializer.serialize(shortMessage(storage, 0x90, 60, 100), wire);
  serializer.serialize(shortMessage(storage, 0x90, 62, 100), wire);
  serializer.serialize(shortMessage(storage, 0x90, 64, 100), wire);

  assert(wire.bytes.size() == 9);
  assert(wire.is({0x90, 60, 100, 0x90, 62, 100, 0x90, 64, 100}));
}

void test_data_bytes_are_masked()
{
  // A stage that computed a value out of range must not be able to put a byte on
  // the wire that a receiver reads as a status.
  espmidi::Serializer serializer;
  Wire wire;

  espmidi::Message message;
  message.status = 0x90;
  message.data1 = 0xff;
  message.data2 = 0x80;
  message.dataLength = 2;
  assert(serializer.serialize(message, wire));

  assert(wire.is({0x90, 0x7f, 0x00}));
}

void test_undefined_status_is_refused()
{
  espmidi::Serializer serializer;
  Wire wire;

  espmidi::Message message;
  message.status = 0xf4; // undefined System Common
  assert(!serializer.serialize(message, wire));
  assert(wire.bytes.empty());
}

void test_stream_framing()
{
  // The payload is all a chunk carries. 0xF0 and 0xF7 belong to the serializer.
  espmidi::Serializer serializer;
  Wire wire;
  const uint8_t head[] = {0x41, 0x10};
  const uint8_t tail[] = {0x42, 0x00};

  assert(!serializer.inStream());
  assert(serializer.serialize(chunkMessage(head, sizeof(head), true, false), wire));
  assert(serializer.inStream());
  assert(serializer.serialize(chunkMessage(tail, sizeof(tail), false, true), wire));
  assert(!serializer.inStream());

  assert(wire.is({0xf0, 0x41, 0x10, 0x42, 0x00, 0xf7}));
}

void test_single_chunk_stream()
{
  espmidi::Serializer serializer;
  Wire wire;
  const uint8_t payload[] = {0x7d, 0x01};

  assert(serializer.serialize(chunkMessage(payload, sizeof(payload), true, true), wire));
  assert(!serializer.inStream());
  assert(wire.is({0xf0, 0x7d, 0x01, 0xf7}));
}

void test_empty_stream()
{
  // A dump can be interrupted so early that the terminating chunk carries no
  // payload at all. The framing still has to be well formed.
  espmidi::Serializer serializer;
  Wire wire;

  serializer.serialize(chunkMessage(nullptr, 0, true, false), wire);
  serializer.serialize(chunkMessage(nullptr, 0, false, true), wire);

  assert(wire.is({0xf0, 0xf7}));
}

void test_continuation_without_a_start_is_refused()
{
  // Writing the payload alone would put loose data bytes on the wire, which the
  // receiver resolves against whatever running status it happens to hold.
  espmidi::Serializer serializer;
  Wire wire;
  const uint8_t payload[] = {0x41};

  assert(!serializer.serialize(chunkMessage(payload, sizeof(payload), false, false), wire));
  assert(wire.bytes.empty());
}

void test_real_time_interleaves_with_a_stream()
{
  // Rule 3 lets System Real-Time through an output that is busy with a stream,
  // so the serializer has to keep the stream open across it.
  espmidi::Serializer serializer;
  uint8_t storage[espmidi::MaxShortMessageBytes] = {};
  Wire wire;
  const uint8_t head[] = {0x41};
  const uint8_t tail[] = {0x42};

  serializer.serialize(chunkMessage(head, sizeof(head), true, false), wire);
  serializer.serialize(shortMessage(storage, 0xf8), wire);
  assert(serializer.inStream());
  serializer.serialize(chunkMessage(tail, sizeof(tail), false, true), wire);

  assert(wire.is({0xf0, 0x41, 0xf8, 0x42, 0xf7}));
}

void test_close_stream()
{
  // The source of a dump disappeared. The device on the other end is holding a
  // partial message and needs the terminator to discard it and carry on.
  espmidi::Serializer serializer;
  Wire wire;
  const uint8_t head[] = {0x41};

  serializer.serialize(chunkMessage(head, sizeof(head), true, false), wire);
  assert(serializer.closeStream(wire));
  assert(!serializer.inStream());
  assert(wire.is({0xf0, 0x41, 0xf7}));

  // Closing again writes nothing: there is no stream to close.
  assert(serializer.closeStream(wire));
  assert(wire.bytes.size() == 3);
}

void test_reset_forgets_an_open_stream()
{
  // The link itself went away, so the terminator could not be sent anyway.
  espmidi::Serializer serializer;
  Wire wire;
  const uint8_t head[] = {0x41};

  serializer.serialize(chunkMessage(head, sizeof(head), true, false), wire);
  serializer.reset();
  assert(!serializer.inStream());
  assert(serializer.closeStream(wire));
  assert(wire.is({0xf0, 0x41}));
}

void test_a_refused_write_is_reported()
{
  espmidi::Serializer serializer;
  uint8_t storage[espmidi::MaxShortMessageBytes] = {};
  Wire wire;
  wire.refuseAfter = 1;

  assert(serializer.serialize(shortMessage(storage, 0x90, 60, 100), wire));
  assert(!serializer.serialize(shortMessage(storage, 0x90, 62, 100), wire));

  // The transport refusing bytes ends the stream rather than leaving the
  // serializer believing it still owes an 0xF7 on a link that is not taking it.
  const uint8_t head[] = {0x41};
  assert(!serializer.serialize(chunkMessage(head, sizeof(head), true, false), wire));
  assert(!serializer.inStream());
}

void test_round_trip_through_the_parser()
{
  // Whatever the parser makes of a stream, the serializer turns back into the
  // same stream — with running status expanded, which is the one difference the
  // two halves are allowed to have.
  const uint8_t input[] = {
      0x90, 60, 100,       // note on
      62,   100,           // the same, by running status
      0xf0, 0x41, 0x10,    // a dump
      0xf8,                // interrupted by a clock
      0x42, 0xf7,          //
      0xb0, 7,    64,      // control change
  };

  espmidi::Parser parser;
  espmidi::Serializer serializer;
  Wire wire;
  parser.parse(input, sizeof(input), [&](const espmidi::Message &message) { serializer.serialize(message, wire); });

  assert(wire.is({
      0x90, 60, 100,
      0x90, 62, 100, // the status byte the sender left out
      0xf0, 0x41, 0x10,
      0xf8,
      0x42, 0xf7,
      0xb0, 7, 64,
  }));
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_short_messages);
  run(test_running_status_is_not_used);
  run(test_data_bytes_are_masked);
  run(test_undefined_status_is_refused);
  run(test_stream_framing);
  run(test_single_chunk_stream);
  run(test_empty_stream);
  run(test_continuation_without_a_start_is_refused);
  run(test_real_time_interleaves_with_a_stream);
  run(test_close_stream);
  run(test_reset_forgets_an_open_stream);
  run(test_a_refused_write_is_reported);
  run(test_round_trip_through_the_parser);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
