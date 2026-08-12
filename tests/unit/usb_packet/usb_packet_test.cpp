// USB MIDI 1.0 event packets to and from espmidi::Message.
//
// Specification: docs/DATA_MODEL.ja.md. This codec is shared by the USB Host and
// USB Device ports, and neither transport library assembles a SysEx, so the
// assembly is fixed here where it can be tested without hardware.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
int g_ran = 0;

struct Captured
{
  espmidi::PortId port;
  espmidi::MessageType type;
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  uint8_t dataLength;
  size_t length;
  bool chunk;
  bool chunkStart;
  bool chunkEnd;
  uint8_t chunkData[8];
  size_t chunkLength;
};

struct Collector
{
  static constexpr size_t Capacity = 64;
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

void decode(espmidi::UsbPacketDecoder &decoder, Collector &collector, const uint8_t *data, size_t length)
{
  decoder.decode(data, length, [&collector](const espmidi::Message &m) { collector(m); });
}

void test_cin_lengths()
{
  assert(espmidi::usbCinLength(espmidi::UsbCin::NoteOn) == 3);
  assert(espmidi::usbCinLength(espmidi::UsbCin::ProgramChange) == 2);
  assert(espmidi::usbCinLength(espmidi::UsbCin::ChannelPressure) == 2);
  assert(espmidi::usbCinLength(espmidi::UsbCin::PitchBend) == 3);
  assert(espmidi::usbCinLength(espmidi::UsbCin::SystemCommon2) == 2);
  assert(espmidi::usbCinLength(espmidi::UsbCin::SystemCommon3) == 3);
  assert(espmidi::usbCinLength(espmidi::UsbCin::SysExStart) == 3);
  assert(espmidi::usbCinLength(espmidi::UsbCin::SysExEnd1) == 1);
  assert(espmidi::usbCinLength(espmidi::UsbCin::SysExEnd2) == 2);
  assert(espmidi::usbCinLength(espmidi::UsbCin::SysExEnd3) == 3);
  assert(espmidi::usbCinLength(espmidi::UsbCin::SingleByte) == 1);

  // The reserved values carry nothing and must be skipped, not guessed at.
  assert(espmidi::usbCinLength(espmidi::UsbCin::MiscFunction) == 0);
  assert(espmidi::usbCinLength(espmidi::UsbCin::CableEvent) == 0);
}

void test_cin_for_status()
{
  // A channel voice CIN is the status byte's high nibble.
  assert(espmidi::usbCinForStatus(0x90) == espmidi::UsbCin::NoteOn);
  assert(espmidi::usbCinForStatus(0x8f) == espmidi::UsbCin::NoteOff);
  assert(espmidi::usbCinForStatus(0xc3) == espmidi::UsbCin::ProgramChange);
  assert(espmidi::usbCinForStatus(0xe0) == espmidi::UsbCin::PitchBend);

  assert(espmidi::usbCinForStatus(0xf1) == espmidi::UsbCin::SystemCommon2);
  assert(espmidi::usbCinForStatus(0xf3) == espmidi::UsbCin::SystemCommon2);
  assert(espmidi::usbCinForStatus(0xf2) == espmidi::UsbCin::SystemCommon3);
  assert(espmidi::usbCinForStatus(0xf6) == espmidi::UsbCin::SysExEnd1);
  assert(espmidi::usbCinForStatus(0xf8) == espmidi::UsbCin::SingleByte);
  assert(espmidi::usbCinForStatus(0xff) == espmidi::UsbCin::SingleByte);
}

void test_decode_channel_voice()
{
  espmidi::UsbPacketDecoder decoder;
  decoder.setCablePort(0, espmidi::PortId{5});
  Collector collector;

  const uint8_t packet[] = {0x09, 0x90, 60, 100};
  decode(decoder, collector, packet, sizeof(packet));

  assert(collector.count == 1);
  const Captured &m = collector.items[0];
  assert(m.port == espmidi::PortId{5});
  assert(m.type == espmidi::MessageType::Midi1ChannelVoice);
  assert(m.status == 0x90);
  assert(m.data1 == 60);
  assert(m.data2 == 100);
  assert(m.dataLength == 2);
  assert(m.length == 3);
  assert(!m.chunk);
}

void test_decode_uses_the_cable_as_the_port()
{
  // A cable is a port. Two cables in one transfer must land on two ports, which
  // is what makes one USB device able to present several MIDI ports.
  espmidi::UsbPacketDecoder decoder;
  decoder.setCablePort(0, espmidi::PortId{10});
  decoder.setCablePort(3, espmidi::PortId{13});
  Collector collector;

  const uint8_t packets[] = {
      0x09, 0x90, 60, 100, // cable 0
      0x39, 0x90, 62, 101, // cable 3
  };
  decode(decoder, collector, packets, sizeof(packets));

  assert(collector.count == 2);
  assert(collector.items[0].port == espmidi::PortId{10});
  assert(collector.items[1].port == espmidi::PortId{13});
  assert(collector.items[1].data1 == 62);

  // A cable with no port assigned still decodes; the message just says it has
  // no port rather than being silently attributed to another one.
  collector.clear();
  const uint8_t other[] = {0x59, 0x90, 64, 102};
  decode(decoder, collector, other, sizeof(other));
  assert(collector.count == 1);
  assert(!collector.items[0].port.valid());
}

void test_decode_short_messages_with_fewer_data_bytes()
{
  espmidi::UsbPacketDecoder decoder;
  Collector collector;

  const uint8_t packets[] = {
      0x0c, 0xc0, 7, 0,     // program change, one data byte
      0x02, 0xf3, 5, 0,     // song select, two-byte system common
      0x03, 0xf2, 1, 2,     // song position, three-byte system common
      0x0f, 0xf8, 0, 0,     // clock, single byte
  };
  decode(decoder, collector, packets, sizeof(packets));

  assert(collector.count == 4);
  assert(collector.items[0].status == 0xc0 && collector.items[0].dataLength == 1);
  assert(collector.items[0].data1 == 7 && collector.items[0].data2 == 0);
  assert(collector.items[1].status == 0xf3 && collector.items[1].dataLength == 1);
  assert(collector.items[2].status == 0xf2 && collector.items[2].dataLength == 2);
  assert(collector.items[2].data1 == 1 && collector.items[2].data2 == 2);
  assert(collector.items[3].status == 0xf8 && collector.items[3].dataLength == 0);
  assert(collector.items[3].type == espmidi::MessageType::System);
}

void test_cin5_is_disambiguated_by_its_byte()
{
  // CIN 0x5 means either "SysEx ends with one byte" or a single-byte System
  // Common. Only an 0xF7 can end a SysEx, so the byte itself decides.
  espmidi::UsbPacketDecoder decoder;
  Collector collector;

  const uint8_t tuneRequest[] = {0x05, 0xf6, 0, 0};
  decode(decoder, collector, tuneRequest, sizeof(tuneRequest));
  assert(collector.count == 1);
  assert(!collector.items[0].chunk);
  assert(collector.items[0].status == 0xf6);

  // The same CIN carrying 0xF7 ends a stream instead.
  collector.clear();
  const uint8_t stream[] = {
      0x04, 0xf0, 0x41, 0x10,
      0x05, 0xf7, 0, 0,
  };
  decode(decoder, collector, stream, sizeof(stream));
  assert(collector.count == 2);
  assert(collector.items[0].chunk && collector.items[0].chunkStart && !collector.items[0].chunkEnd);
  assert(collector.items[1].chunk && collector.items[1].chunkEnd);
  assert(collector.items[1].chunkLength == 0);
}

void test_decode_sysex_strips_framing()
{
  espmidi::UsbPacketDecoder decoder;
  decoder.setCablePort(0, espmidi::PortId{1});
  Collector collector;

  // F0 41 10 / 42 43 44 / 45 F7
  const uint8_t packets[] = {
      0x04, 0xf0, 0x41, 0x10,
      0x04, 0x42, 0x43, 0x44,
      0x06, 0x45, 0xf7, 0x00,
  };
  decode(decoder, collector, packets, sizeof(packets));

  assert(collector.count == 3);
  assert(collector.items[0].chunkStart && !collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 2); // 0xF0 is framing, not payload
  assert(collector.items[0].chunkData[0] == 0x41 && collector.items[0].chunkData[1] == 0x10);
  assert(collector.items[0].type == espmidi::MessageType::Data7);
  assert(collector.items[0].port == espmidi::PortId{1});

  assert(!collector.items[1].chunkStart && !collector.items[1].chunkEnd);
  assert(collector.items[1].chunkLength == 3);

  assert(!collector.items[2].chunkStart && collector.items[2].chunkEnd);
  assert(collector.items[2].chunkLength == 1); // 0xF7 is framing
  assert(collector.items[2].chunkData[0] == 0x45);
  assert(!decoder.inSysEx(0));
}

void test_decode_complete_sysex_in_one_packet()
{
  espmidi::UsbPacketDecoder decoder;
  Collector collector;

  // F0 41 F7 in a single CIN 0x7 packet: both framing bytes are stripped.
  const uint8_t packet[] = {0x07, 0xf0, 0x41, 0xf7};
  decode(decoder, collector, packet, sizeof(packet));

  assert(collector.count == 1);
  assert(collector.items[0].chunkStart && collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 1);
  assert(collector.items[0].chunkData[0] == 0x41);

  // An empty SysEx, F0 F7, leaves no payload at all.
  collector.clear();
  const uint8_t empty[] = {0x06, 0xf0, 0xf7, 0x00};
  decode(decoder, collector, empty, sizeof(empty));
  assert(collector.count == 1);
  assert(collector.items[0].chunkStart && collector.items[0].chunkEnd);
  assert(collector.items[0].chunkLength == 0);
}

void test_sysex_state_is_per_cable()
{
  // Cables are independent ports and a bulk transfer may interleave them, so one
  // cable's dump must not be attached to another's.
  espmidi::UsbPacketDecoder decoder;
  decoder.setCablePort(0, espmidi::PortId{20});
  decoder.setCablePort(1, espmidi::PortId{21});
  Collector collector;

  const uint8_t packets[] = {
      0x04, 0xf0, 0x41, 0x01, // cable 0 starts
      0x14, 0xf0, 0x42, 0x02, // cable 1 starts
      0x06, 0x03, 0xf7, 0x00, // cable 0 ends
      0x16, 0x04, 0xf7, 0x00, // cable 1 ends
  };
  decode(decoder, collector, packets, sizeof(packets));

  assert(collector.count == 4);
  assert(collector.items[0].port == espmidi::PortId{20} && collector.items[0].chunkStart);
  assert(collector.items[1].port == espmidi::PortId{21} && collector.items[1].chunkStart);
  assert(collector.items[2].port == espmidi::PortId{20} && collector.items[2].chunkEnd);
  assert(collector.items[3].port == espmidi::PortId{21} && collector.items[3].chunkEnd);
  assert(!decoder.inSysEx(0) && !decoder.inSysEx(1));
}

void test_continuation_without_a_start_is_dropped()
{
  // After a reset in the middle of a dump there is no start to report, so the
  // bytes are dropped rather than emitted as a chunk with no beginning.
  espmidi::UsbPacketDecoder decoder;
  Collector collector;

  const uint8_t packet[] = {0x04, 0x41, 0x42, 0x43};
  decode(decoder, collector, packet, sizeof(packet));
  assert(collector.count == 0);

  // The same applies after an explicit reset mid-stream.
  const uint8_t start[] = {0x04, 0xf0, 0x41, 0x42};
  decode(decoder, collector, start, sizeof(start));
  assert(collector.count == 1);
  assert(decoder.inSysEx(0));
  decoder.resetCable(0);
  assert(!decoder.inSysEx(0));
  collector.clear();
  decode(decoder, collector, packet, sizeof(packet));
  assert(collector.count == 0);
}

void test_reserved_cin_and_partial_packet_are_ignored()
{
  espmidi::UsbPacketDecoder decoder;
  Collector collector;

  const uint8_t reserved[] = {0x00, 0x90, 60, 100};
  decode(decoder, collector, reserved, sizeof(reserved));
  assert(collector.count == 0);

  // A transfer that is not a whole number of packets: the trailing fragment is
  // ignored rather than read past the end.
  const uint8_t partial[] = {0x09, 0x90, 60, 100, 0x09, 0x90};
  decode(decoder, collector, partial, sizeof(partial));
  assert(collector.count == 1);

  decode(decoder, collector, nullptr, 4);
  assert(collector.count == 1);
}

void test_encode_short_message()
{
  espmidi::UsbPacketEncoder encoder(2);
  assert(encoder.cable() == 2);

  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message message;
  espmidi::buildShortMessage(message, bytes, 0x90, 60, 100);

  uint8_t packet[8] = {};
  assert(encoder.encode(message, packet, sizeof(packet)) == 4);
  assert(packet[0] == 0x29); // cable 2, CIN 0x9
  assert(packet[1] == 0x90 && packet[2] == 60 && packet[3] == 100);

  // Unused bytes are zeroed rather than left holding whatever was there.
  espmidi::buildShortMessage(message, bytes, 0xc0, 7);
  assert(encoder.encode(message, packet, sizeof(packet)) == 4);
  assert(packet[0] == 0x2c && packet[1] == 0xc0 && packet[2] == 7 && packet[3] == 0);

  espmidi::buildShortMessage(message, bytes, 0xf8);
  assert(encoder.encode(message, packet, sizeof(packet)) == 4);
  assert(packet[0] == 0x2f && packet[1] == 0xf8 && packet[2] == 0 && packet[3] == 0);

  // A buffer too small to hold the worst case fails before writing.
  uint8_t small[3] = {0xee, 0xee, 0xee};
  assert(encoder.encode(message, small, sizeof(small)) == 0);
  assert(small[0] == 0xee);
}

espmidi::Message makeChunk(const uint8_t *data, size_t length, bool start, bool end)
{
  espmidi::Message message;
  message.type = espmidi::MessageType::Data7;
  message.status = 0xf0;
  message.chunk = true;
  message.chunkStart = start;
  message.chunkEnd = end;
  message.chunkData = data;
  message.chunkLength = length;
  return message;
}

void test_encode_sysex_groups_of_three()
{
  espmidi::UsbPacketEncoder encoder(0);
  uint8_t out[64] = {};

  // Two payload bytes plus the 0xF0 make exactly one full group, and the 0xF7
  // then needs a packet of its own.
  const uint8_t payload[] = {0x41, 0x10};
  const espmidi::Message chunk = makeChunk(payload, sizeof(payload), true, true);
  const size_t written = encoder.encode(chunk, out, sizeof(out));

  assert(written == 8);
  assert(out[0] == 0x04 && out[1] == 0xf0 && out[2] == 0x41 && out[3] == 0x10);
  assert(out[4] == 0x05 && out[5] == 0xf7); // ends with a single byte
  assert(!encoder.inSysEx());
}

void test_encode_sysex_end_cins()
{
  uint8_t out[64] = {};

  // One payload byte: F0 xx F7 fits one packet, so the stream ends with three.
  {
    espmidi::UsbPacketEncoder encoder(0);
    const uint8_t payload[] = {0x41};
    const size_t written = encoder.encode(makeChunk(payload, 1, true, true), out, sizeof(out));
    assert(written == 4);
    assert(out[0] == 0x07 && out[1] == 0xf0 && out[2] == 0x41 && out[3] == 0xf7);
  }

  // No payload: F0 F7 ends with two.
  {
    espmidi::UsbPacketEncoder encoder(0);
    const size_t written = encoder.encode(makeChunk(nullptr, 0, true, true), out, sizeof(out));
    assert(written == 4);
    assert(out[0] == 0x06 && out[1] == 0xf0 && out[2] == 0xf7 && out[3] == 0x00);
  }

  // Three payload bytes: F0 and the first two fill a group, the rest ends with
  // two. This is the case where appending the terminator through the normal
  // path would emit an empty extra packet.
  {
    espmidi::UsbPacketEncoder encoder(0);
    const uint8_t payload[] = {0x41, 0x42, 0x43};
    const size_t written = encoder.encode(makeChunk(payload, 3, true, true), out, sizeof(out));
    assert(written == 8);
    assert(out[0] == 0x04 && out[1] == 0xf0 && out[2] == 0x41 && out[3] == 0x42);
    assert(out[4] == 0x06 && out[5] == 0x43 && out[6] == 0xf7 && out[7] == 0x00);
  }

  // Five payload bytes: two full groups and then a lone terminator.
  {
    espmidi::UsbPacketEncoder encoder(0);
    const uint8_t payload[] = {0x41, 0x42, 0x43, 0x44, 0x45};
    const size_t written = encoder.encode(makeChunk(payload, 5, true, true), out, sizeof(out));
    assert(written == 12);
    assert(out[8] == 0x05 && out[9] == 0xf7);
  }
}

void test_encode_sysex_across_chunks()
{
  // A chunk boundary is not a packet boundary: leftover bytes are carried into
  // the next chunk rather than being padded out.
  espmidi::UsbPacketEncoder encoder(1);
  uint8_t out[64] = {};

  const uint8_t first[] = {0x41};
  size_t written = encoder.encode(makeChunk(first, 1, true, false), out, sizeof(out));
  assert(written == 0); // only 0xF0 and one byte pending, no full group yet
  assert(encoder.inSysEx());

  const uint8_t second[] = {0x42, 0x43};
  written = encoder.encode(makeChunk(second, 2, false, false), out, sizeof(out));
  assert(written == 4);
  assert(out[0] == 0x14 && out[1] == 0xf0 && out[2] == 0x41 && out[3] == 0x42);

  const uint8_t third[] = {0x44};
  written = encoder.encode(makeChunk(third, 1, false, true), out, sizeof(out));
  assert(written == 4);
  assert(out[0] == 0x17 && out[1] == 0x43 && out[2] == 0x44 && out[3] == 0xf7);
  assert(!encoder.inSysEx());

  // A continuation with no stream open produces nothing rather than a chunk
  // that starts in the middle.
  written = encoder.encode(makeChunk(third, 1, false, true), out, sizeof(out));
  assert(written == 0);
}

void test_round_trip()
{
  // What the encoder writes is what the decoder reads back, framing and all.
  const uint8_t payload[] = {0x41, 0x10, 0x42, 0x00, 0x7f, 0x33, 0x44};

  espmidi::UsbPacketEncoder encoder(4);
  uint8_t packets[64] = {};
  const espmidi::Message chunk = makeChunk(payload, sizeof(payload), true, true);
  const size_t written = encoder.encode(chunk, packets, sizeof(packets));
  assert(written > 0);

  espmidi::UsbPacketDecoder decoder;
  decoder.setCablePort(4, espmidi::PortId{9});
  Collector collector;
  decode(decoder, collector, packets, written);

  uint8_t rebuilt[sizeof(payload)] = {};
  size_t offset = 0;
  assert(collector.count > 0);
  for (size_t i = 0; i < collector.count; i++)
  {
    const Captured &m = collector.items[i];
    assert(m.chunk);
    assert(m.port == espmidi::PortId{9});
    assert(m.chunkStart == (i == 0));
    assert(m.chunkEnd == (i == collector.count - 1));
    assert(offset + m.chunkLength <= sizeof(rebuilt));
    std::memcpy(&rebuilt[offset], m.chunkData, m.chunkLength);
    offset += m.chunkLength;
  }
  assert(offset == sizeof(payload));
  assert(std::memcmp(rebuilt, payload, sizeof(payload)) == 0);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_cin_lengths);
  run(test_cin_for_status);
  run(test_decode_channel_voice);
  run(test_decode_uses_the_cable_as_the_port);
  run(test_decode_short_messages_with_fewer_data_bytes);
  run(test_cin5_is_disambiguated_by_its_byte);
  run(test_decode_sysex_strips_framing);
  run(test_decode_complete_sysex_in_one_packet);
  run(test_sysex_state_is_per_cable);
  run(test_continuation_without_a_start_is_dropped);
  run(test_reserved_cin_and_partial_packet_are_ignored);
  run(test_encode_short_message);
  run(test_encode_sysex_groups_of_three);
  run(test_encode_sysex_end_cins);
  run(test_encode_sysex_across_chunks);
  run(test_round_trip);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
