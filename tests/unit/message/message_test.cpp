// The common representation: the status-byte classification, the data length
// table, and building and serialising short messages.
//
// Specification: docs/DATA_MODEL.ja.md.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>

namespace
{
int g_ran = 0;

void test_status_classification()
{
  assert(espmidi::isStatusByte(0x90));
  assert(!espmidi::isStatusByte(0x7f));
  assert(espmidi::isDataByte(0x00));
  assert(espmidi::isDataByte(0x7f));

  assert(espmidi::isChannelVoice(0x80));
  assert(espmidi::isChannelVoice(0xef));
  assert(!espmidi::isChannelVoice(0xf0));
  assert(!espmidi::isChannelVoice(0x7f));

  // 0xF1..0xF7 are System Common, 0xF8..0xFF System Real-Time. The boundary is
  // what decides whether running status survives, so it is pinned here.
  assert(espmidi::isSystemCommon(0xf1));
  assert(espmidi::isSystemCommon(0xf7));
  assert(!espmidi::isSystemCommon(0xf8));
  assert(espmidi::isSystemRealTime(0xf8));
  assert(espmidi::isSystemRealTime(0xff));
  assert(!espmidi::isSystemRealTime(0xf7));

  assert(espmidi::isSysExStart(0xf0));
  assert(espmidi::isSysExEnd(0xf7));
}

void test_data_length_table()
{
  // Channel voice: two data bytes except program change and channel pressure.
  for (uint8_t channel = 0; channel < 16; channel++)
  {
    assert(espmidi::messageDataLength(static_cast<uint8_t>(0x80 | channel)) == 2); // note off
    assert(espmidi::messageDataLength(static_cast<uint8_t>(0x90 | channel)) == 2); // note on
    assert(espmidi::messageDataLength(static_cast<uint8_t>(0xa0 | channel)) == 2); // poly pressure
    assert(espmidi::messageDataLength(static_cast<uint8_t>(0xb0 | channel)) == 2); // control change
    assert(espmidi::messageDataLength(static_cast<uint8_t>(0xc0 | channel)) == 1); // program change
    assert(espmidi::messageDataLength(static_cast<uint8_t>(0xd0 | channel)) == 1); // channel pressure
    assert(espmidi::messageDataLength(static_cast<uint8_t>(0xe0 | channel)) == 2); // pitch bend
  }

  assert(espmidi::messageDataLength(0xf1) == 1); // quarter frame
  assert(espmidi::messageDataLength(0xf2) == 2); // song position
  assert(espmidi::messageDataLength(0xf3) == 1); // song select
  assert(espmidi::messageDataLength(0xf6) == 0); // tune request

  // System Real-Time carries nothing.
  for (int status = 0xf8; status <= 0xff; status++)
  {
    assert(espmidi::messageDataLength(static_cast<uint8_t>(status)) == 0);
  }

  // The negative results a caller has to branch on rather than count.
  assert(espmidi::messageDataLength(0x40) == -1); // data byte, not a status
  assert(espmidi::messageDataLength(0xf0) == -2); // SysEx start
  assert(espmidi::messageDataLength(0xf7) == -3); // SysEx end
  assert(espmidi::messageDataLength(0xf4) == -4); // undefined
  assert(espmidi::messageDataLength(0xf5) == -4); // undefined
}

void test_message_type_follows_ump_numbering()
{
  // The numbers are UMP Message Types, so MIDI 2.0 can be added without
  // renumbering (docs/DECISIONS.ja.md, decision 1).
  assert(static_cast<uint8_t>(espmidi::MessageType::Utility) == 0x0);
  assert(static_cast<uint8_t>(espmidi::MessageType::System) == 0x1);
  assert(static_cast<uint8_t>(espmidi::MessageType::Midi1ChannelVoice) == 0x2);
  assert(static_cast<uint8_t>(espmidi::MessageType::Data7) == 0x3);

  assert(espmidi::messageTypeForStatus(0x90) == espmidi::MessageType::Midi1ChannelVoice);
  assert(espmidi::messageTypeForStatus(0xef) == espmidi::MessageType::Midi1ChannelVoice);
  assert(espmidi::messageTypeForStatus(0xf0) == espmidi::MessageType::Data7);
  assert(espmidi::messageTypeForStatus(0xf7) == espmidi::MessageType::Data7);
  // UMP puts System Common and System Real-Time in the same message type.
  assert(espmidi::messageTypeForStatus(0xf2) == espmidi::MessageType::System);
  assert(espmidi::messageTypeForStatus(0xf8) == espmidi::MessageType::System);
}

void test_channel_and_command()
{
  espmidi::Message message;
  message.status = 0x93;
  assert(message.channel() == 3);
  assert(message.command() == 0x90);
  assert(message.isChannelVoiceMessage());
  assert(!message.isSystemRealTimeMessage());

  // Only channel voice messages have a channel; a system message must not
  // appear to be on channel 8 just because its status ends in 8.
  message.status = 0xf8;
  assert(message.channel() == 0);
  assert(message.command() == 0xf8);
  assert(!message.isChannelVoiceMessage());
  assert(message.isSystemRealTimeMessage());
}

void test_port_id()
{
  espmidi::PortId unset;
  assert(!unset.valid());

  espmidi::PortId a{3};
  espmidi::PortId b{3};
  espmidi::PortId c{4};
  assert(a.valid());
  assert(a == b);
  assert(a != c);

  // The channel space is (port, channel), so 16 ports of 16 channels have to be
  // distinguishable — the property that keeps MIDI 2.0 groups from needing a
  // different coordinate (docs/DATA_MODEL.ja.md).
  assert(espmidi::MaxPortsPerEndpoint == 16);
}

void test_timestamp_carries_its_unit()
{
  espmidi::Timestamp none;
  assert(!none.present());
  assert(none.unit == espmidi::TimestampUnit::None);

  espmidi::Timestamp ble{1234, espmidi::TimestampUnit::Milliseconds13};
  assert(ble.present());

  // The same number in different units is not the same instant.
  espmidi::Timestamp jr{1234, espmidi::TimestampUnit::JrTicks31250};
  assert(ble != jr);
  assert(ble == (espmidi::Timestamp{1234, espmidi::TimestampUnit::Milliseconds13}));

  // Every absent timestamp is equal, whatever value happens to sit in the field,
  // so a port that leaves the value alone cannot make two of them differ.
  espmidi::Timestamp noneWithValue{99, espmidi::TimestampUnit::None};
  assert(none == noneWithValue);
}

void test_build_short_message()
{
  uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message message;

  assert(espmidi::buildShortMessage(message, bytes, 0x90, 60, 100) == 3);
  assert(message.status == 0x90);
  assert(message.data1 == 60);
  assert(message.data2 == 100);
  assert(message.dataLength == 2);
  assert(message.length == 3);
  assert(message.raw == bytes);
  assert(bytes[0] == 0x90 && bytes[1] == 60 && bytes[2] == 100);
  assert(message.type == espmidi::MessageType::Midi1ChannelVoice);
  assert(!message.chunk);

  // One data byte: the message is two bytes long even though the buffer holds
  // three.
  assert(espmidi::buildShortMessage(message, bytes, 0xc0, 7) == 2);
  assert(message.dataLength == 1);
  assert(message.length == 2);

  // No data bytes.
  assert(espmidi::buildShortMessage(message, bytes, 0xf8) == 1);
  assert(message.dataLength == 0);
  assert(message.length == 1);
  assert(message.type == espmidi::MessageType::System);

  // Data bytes are masked to 7 bits, so a caller cannot produce a byte that
  // would be read back as a status byte.
  assert(espmidi::buildShortMessage(message, bytes, 0x90, 0xff, 0x81) == 3);
  assert(bytes[1] == 0x7f);
  assert(bytes[2] == 0x01);

  // A status byte with no fixed length is refused rather than guessed at.
  assert(espmidi::buildShortMessage(message, bytes, 0xf0) == 0);
  assert(espmidi::buildShortMessage(message, bytes, 0x40) == 0);
  assert(espmidi::buildShortMessage(message, nullptr, 0x90, 60, 100) == 0);
}

void test_serialize_short_message()
{
  uint8_t source[espmidi::MaxShortMessageBytes] = {};
  espmidi::Message message;
  espmidi::buildShortMessage(message, source, 0xb0, 7, 64);

  uint8_t out[espmidi::MaxShortMessageBytes] = {};
  assert(espmidi::serializeShortMessage(message, out, sizeof(out)) == 3);
  assert(out[0] == 0xb0 && out[1] == 7 && out[2] == 64);

  // Too small a buffer fails instead of writing part of a message.
  uint8_t small[2] = {0xee, 0xee};
  assert(espmidi::serializeShortMessage(message, small, sizeof(small)) == 0);
  assert(small[0] == 0xee && small[1] == 0xee);

  // A chunk is not serialised here: its framing is the transport's decision.
  espmidi::Message chunk;
  chunk.chunk = true;
  chunk.status = 0xf0;
  assert(espmidi::serializeShortMessage(chunk, out, sizeof(out)) == 0);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_status_classification);
  run(test_data_length_table);
  run(test_message_type_follows_ump_numbering);
  run(test_channel_and_command);
  run(test_port_id);
  run(test_timestamp_carries_its_unit);
  run(test_build_short_message);
  run(test_serialize_short_message);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
