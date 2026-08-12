// The common MIDI message representation.
//
// Everything inside EspMidi is expressed in these terms: ports produce them,
// routing and filtering read them, ports consume them. The shape is fixed in
// docs/DATA_MODEL.ja.md and the reasoning is in docs/DECISIONS.ja.md (decision 1).
//
// Two properties are worth keeping in mind while reading this file.
//
// The representation carries MIDI 1.0 bytes but is laid out so MIDI 2.0 does not
// force a redesign: MessageType is numbered like a UMP Message Type, the routing
// coordinate is (PortId, channel) rather than channel alone, the timestamp
// carries its unit, and chunking is not a SysEx-only concept.
//
// Pointers are valid only for the duration of the callback that hands the
// message over. That is what lets a long SysEx pass through without being
// copied; anything that outlives the call must copy.

#ifndef ESPMIDI_MESSAGE_H
#define ESPMIDI_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

namespace espmidi
{

// Message kinds, numbered like a UMP Message Type so MIDI 2.0 additions slot in
// without renumbering (docs/DATA_MODEL.ja.md). Only System, Midi1ChannelVoice
// and Data7 occur today.
enum class MessageType : uint8_t
{
  Utility = 0x0,
  System = 0x1,            // System Common and System Real-Time
  Midi1ChannelVoice = 0x2,
  Data7 = 0x3,             // SysEx7
  // Reserved for MIDI 2.0: Midi2ChannelVoice = 0x4, Data128 = 0x5,
  // FlexData = 0xD, Stream = 0xF.
};

// EspMidi carries a timestamp but never interprets it, so the unit travels with
// the value instead of being normalised away. BLE MIDI supplies a 13-bit
// millisecond counter; MIDI 2.0's JR Timestamp would be JrTicks31250.
enum class TimestampUnit : uint8_t
{
  None = 0,
  Milliseconds13,
  JrTicks31250,
};

struct Timestamp
{
  uint16_t value = 0;
  TimestampUnit unit = TimestampUnit::None;

  bool present() const { return unit != TimestampUnit::None; }
};

inline bool operator==(const Timestamp &a, const Timestamp &b)
{
  // Values in different units are never equal, and every absent timestamp is the
  // same absent timestamp whatever value happens to sit in the field.
  if (a.unit != b.unit)
  {
    return false;
  }
  return a.unit == TimestampUnit::None || a.value == b.value;
}

inline bool operator!=(const Timestamp &a, const Timestamp &b) { return !(a == b); }

// A port is one routing coordinate: one USB cable, one BLE connection, one UART.
// It is the concept a MIDI 2.0 group maps onto, which is why the channel space
// is (PortId, channel) rather than a bare 4-bit channel.
//
// The handle is opaque and assigned by the library; the port model that fills it
// in arrives with the port registry (docs/DEVELOPMENT_PLAN.ja.md, phase 2). Until
// then it identifies a message's origin and nothing more.
struct PortId
{
  static constexpr uint16_t Invalid = 0xffff;

  uint16_t value = Invalid;

  bool valid() const { return value != Invalid; }
};

inline bool operator==(const PortId &a, const PortId &b) { return a.value == b.value; }
inline bool operator!=(const PortId &a, const PortId &b) { return !(a == b); }

// USB MIDI 1.0 addresses cables with 4 bits and a UMP group is 4 bits too, so an
// endpoint carries at most this many ports either way.
static constexpr uint8_t MaxPortsPerEndpoint = 16;

// The longest MIDI 1.0 message that is not a data stream: status plus two data
// bytes. SysEx is delivered in chunks instead and is not bounded by this.
static constexpr size_t MaxShortMessageBytes = 3;

// --- Status byte classification ------------------------------------------
//
// These are pure functions of a status byte. Ports and the parser share them so
// the same byte is never classified two different ways.

inline bool isStatusByte(uint8_t byte) { return (byte & 0x80) != 0; }
inline bool isDataByte(uint8_t byte) { return (byte & 0x80) == 0; }

// 0xF8..0xFF. These may appear between the bytes of another message and must not
// disturb it, which is why they are checked before anything else.
inline bool isSystemRealTime(uint8_t status) { return status >= 0xf8; }

// 0xF1..0xF7. A System Common message cancels running status.
inline bool isSystemCommon(uint8_t status) { return status >= 0xf1 && status <= 0xf7; }

// 0x80..0xEF. The only messages that carry a channel.
inline bool isChannelVoice(uint8_t status) { return status >= 0x80 && status < 0xf0; }

inline bool isSysExStart(uint8_t status) { return status == 0xf0; }
inline bool isSysExEnd(uint8_t status) { return status == 0xf7; }

// Data bytes a status byte is followed by: 0, 1 or 2 for the fixed-length
// messages. The negative results are the cases a caller has to handle rather
// than count:
//
//   -1  not a status byte
//   -2  SysEx start (0xF0), a stream with no fixed length
//   -3  SysEx end (0xF7)
//   -4  undefined status (0xF4, 0xF5), which devices are allowed to emit and
//       receivers are required to ignore
inline int messageDataLength(uint8_t status)
{
  if (!isStatusByte(status))
  {
    return -1;
  }
  if (status < 0xf0)
  {
    // 0xC0 program change and 0xD0 channel pressure carry one byte; the other
    // channel voice messages carry two.
    const uint8_t command = status & 0xf0;
    return (command == 0xc0 || command == 0xd0) ? 1 : 2;
  }
  switch (status)
  {
  case 0xf0:
    return -2;
  case 0xf1: // MIDI Time Code quarter frame
    return 1;
  case 0xf2: // Song Position Pointer
    return 2;
  case 0xf3: // Song Select
    return 1;
  case 0xf4:
  case 0xf5:
    return -4;
  case 0xf6: // Tune Request
    return 0;
  case 0xf7:
    return -3;
  default: // 0xF8..0xFF, System Real-Time
    return 0;
  }
}

// The UMP message type a MIDI 1.0 status byte belongs to. SysEx bytes are not
// classified here because a chunk is identified by the message's chunk flags
// rather than by a status byte.
inline MessageType messageTypeForStatus(uint8_t status)
{
  if (isChannelVoice(status))
  {
    return MessageType::Midi1ChannelVoice;
  }
  if (isSysExStart(status) || isSysExEnd(status))
  {
    return MessageType::Data7;
  }
  return MessageType::System;
}

// --- The message ----------------------------------------------------------

struct Message
{
  PortId port;
  MessageType type = MessageType::Midi1ChannelVoice;
  Timestamp timestamp;

  // The full status byte, with running status already resolved, so a consumer
  // never has to remember what came before.
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  uint8_t dataLength = 0; // valid data bytes, 0..2

  // The wire bytes of this message: status followed by its data bytes. Valid
  // only for the duration of the callback.
  const uint8_t *raw = nullptr;
  size_t length = 0;

  // A chunk of a data message. Deliberately not named after SysEx: SysEx7,
  // SysEx8 and Flex Data all chunk the same way, so MIDI 2.0 reuses these
  // fields rather than adding parallel ones.
  //
  // chunkData excludes the 0xF0 / 0xF7 framing, and is valid only for the
  // duration of the callback. A stream is one chunk with both chunkStart and
  // chunkEnd set, or a run of chunks where only the first and last carry them.
  bool chunk = false;
  bool chunkStart = false;
  bool chunkEnd = false;
  const uint8_t *chunkData = nullptr;
  size_t chunkLength = 0;

  // 0..15 for a channel voice message, 0 otherwise. Note that this alone is not
  // a routing coordinate: (port, channel) is.
  uint8_t channel() const { return isChannelVoice(status) ? static_cast<uint8_t>(status & 0x0f) : 0; }

  // The command nibble for a channel voice message (0x80, 0x90, ...), or the
  // whole status byte for everything else.
  uint8_t command() const { return isChannelVoice(status) ? static_cast<uint8_t>(status & 0xf0) : status; }

  bool isChannelVoiceMessage() const { return isChannelVoice(status); }
  bool isSystemRealTimeMessage() const { return isSystemRealTime(status); }
};

// --- Building short messages ---------------------------------------------
//
// The wire bytes live in the caller's buffer rather than inside Message, which
// keeps Message pointer-only and makes the lifetime rule uniform: a message
// never owns bytes, whoever built it does.

// Writes the wire bytes of a short (non-SysEx) message into `dst` and points the
// message at them. `dst` must hold MaxShortMessageBytes and must outlive every
// use of the message. Returns the number of bytes written, or 0 if the status
// byte does not describe a fixed-length message.
inline size_t buildShortMessage(Message &message,
                                uint8_t *dst,
                                uint8_t status,
                                uint8_t data1 = 0,
                                uint8_t data2 = 0)
{
  const int dataLength = messageDataLength(status);
  if (!dst || dataLength < 0)
  {
    return 0;
  }

  // Data bytes are 7-bit by definition; masking here means a caller cannot
  // produce bytes that would be read back as a status byte.
  dst[0] = status;
  dst[1] = static_cast<uint8_t>(data1 & 0x7f);
  dst[2] = static_cast<uint8_t>(data2 & 0x7f);

  message.type = messageTypeForStatus(status);
  message.status = status;
  message.data1 = dst[1];
  message.data2 = dst[2];
  message.dataLength = static_cast<uint8_t>(dataLength);
  message.raw = dst;
  message.length = static_cast<size_t>(dataLength) + 1;
  message.chunk = false;
  message.chunkStart = false;
  message.chunkEnd = false;
  message.chunkData = nullptr;
  message.chunkLength = 0;
  return message.length;
}

// Copies the wire bytes of a short message into `dst`, for a port that has to
// hand bytes to a transport. Returns the number of bytes written, or 0 if the
// message is a chunk (chunks are written from chunkData, with framing the
// transport decides) or does not fit.
inline size_t serializeShortMessage(const Message &message, uint8_t *dst, size_t capacity)
{
  if (!dst || message.chunk)
  {
    return 0;
  }
  const int dataLength = messageDataLength(message.status);
  if (dataLength < 0)
  {
    return 0;
  }
  const size_t length = static_cast<size_t>(dataLength) + 1;
  if (capacity < length)
  {
    return 0;
  }
  dst[0] = message.status;
  if (length > 1)
  {
    dst[1] = static_cast<uint8_t>(message.data1 & 0x7f);
  }
  if (length > 2)
  {
    dst[2] = static_cast<uint8_t>(message.data2 & 0x7f);
  }
  return length;
}

} // namespace espmidi

#endif // ESPMIDI_MESSAGE_H
