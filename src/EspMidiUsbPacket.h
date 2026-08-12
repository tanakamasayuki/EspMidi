// USB MIDI 1.0 event packets to and from espmidi::Message.
//
// A USB MIDI event packet is four bytes: a header holding the cable number in
// the high nibble and a Code Index Number in the low nibble, then up to three
// message bytes. The CIN already states how many bytes follow and whether they
// start, continue or end a SysEx, so unlike a raw byte stream there is no
// running status to resolve and no need to look ahead.
//
// Both USB ports share this codec. EspUsbHost hands over the raw four bytes of
// each received packet, EspUsbDevice's readPacket() / writePacket() take the
// same four bytes, and neither library assembles a SysEx — that is done here, so
// it is done once and is testable on the host.
//
// A cable is a port (docs/DATA_MODEL.ja.md): the decoder maps each of the 16
// cables to the PortId the port registry assigned it.

#ifndef ESPMIDI_USB_PACKET_H
#define ESPMIDI_USB_PACKET_H

#include "EspMidiMessage.h"

namespace espmidi
{

// Code Index Numbers, named as in the USB MIDI 1.0 class specification.
enum class UsbCin : uint8_t
{
  MiscFunction = 0x0,     // reserved
  CableEvent = 0x1,       // reserved
  SystemCommon2 = 0x2,    // two-byte System Common
  SystemCommon3 = 0x3,    // three-byte System Common
  SysExStart = 0x4,       // SysEx starts or continues, three bytes
  SysExEnd1 = 0x5,        // SysEx ends with one byte, or single-byte System Common
  SysExEnd2 = 0x6,        // SysEx ends with two bytes
  SysExEnd3 = 0x7,        // SysEx ends with three bytes
  NoteOff = 0x8,
  NoteOn = 0x9,
  PolyKeyPressure = 0xa,
  ControlChange = 0xb,
  ProgramChange = 0xc,
  ChannelPressure = 0xd,
  PitchBend = 0xe,
  SingleByte = 0xf,       // System Real-Time, and any other lone byte
};

static constexpr size_t UsbPacketBytes = 4;

inline uint8_t usbPacketCable(const uint8_t *packet) { return static_cast<uint8_t>(packet[0] >> 4); }
inline UsbCin usbPacketCin(const uint8_t *packet) { return static_cast<UsbCin>(packet[0] & 0x0f); }

// Message bytes a CIN is followed by, 0 for the reserved values.
inline uint8_t usbCinLength(UsbCin cin)
{
  switch (cin)
  {
  case UsbCin::SystemCommon2:
  case UsbCin::SysExEnd2:
  case UsbCin::ProgramChange:
  case UsbCin::ChannelPressure:
    return 2;
  case UsbCin::SystemCommon3:
  case UsbCin::SysExStart:
  case UsbCin::SysExEnd3:
  case UsbCin::NoteOff:
  case UsbCin::NoteOn:
  case UsbCin::PolyKeyPressure:
  case UsbCin::ControlChange:
  case UsbCin::PitchBend:
    return 3;
  case UsbCin::SysExEnd1:
  case UsbCin::SingleByte:
    return 1;
  default:
    return 0;
  }
}

inline bool usbCinIsSysEx(UsbCin cin)
{
  return cin == UsbCin::SysExStart || cin == UsbCin::SysExEnd1 ||
         cin == UsbCin::SysExEnd2 || cin == UsbCin::SysExEnd3;
}

// The CIN to send a short message with. Returns SingleByte for anything that is
// not a fixed-length MIDI 1.0 message, which is also the right answer for a
// System Real-Time byte.
inline UsbCin usbCinForStatus(uint8_t status)
{
  if (isChannelVoice(status))
  {
    return static_cast<UsbCin>(status >> 4);
  }
  switch (status)
  {
  case 0xf1: // MIDI Time Code quarter frame
  case 0xf3: // Song Select
    return UsbCin::SystemCommon2;
  case 0xf2: // Song Position Pointer
    return UsbCin::SystemCommon3;
  case 0xf6: // Tune Request
    return UsbCin::SysExEnd1;
  default:
    return UsbCin::SingleByte;
  }
}

// --- Decoding -------------------------------------------------------------

// Packets to messages. One instance covers one USB MIDI endpoint: the SysEx
// state is kept per cable, because cables are independent ports and a device may
// interleave packets from several of them in one bulk transfer.
class UsbPacketDecoder
{
public:
  void reset()
  {
    for (uint8_t cable = 0; cable < MaxPortsPerEndpoint; cable++)
    {
      state_[cable] = CableState();
    }
  }

  void resetCable(uint8_t cable)
  {
    if (cable < MaxPortsPerEndpoint)
    {
      state_[cable] = CableState();
    }
  }

  // Maps a cable to the port the registry assigned it. Cables with no port set
  // still decode; their messages carry an invalid PortId.
  void setCablePort(uint8_t cable, PortId port)
  {
    if (cable < MaxPortsPerEndpoint)
    {
      state_[cable].port = port;
    }
  }

  PortId cablePort(uint8_t cable) const
  {
    return cable < MaxPortsPerEndpoint ? state_[cable].port : PortId();
  }

  bool inSysEx(uint8_t cable) const
  {
    return cable < MaxPortsPerEndpoint ? state_[cable].inSysEx : false;
  }

  // Decodes one four-byte packet. Pointers in the emitted message point into
  // `packet`, so they are valid only for the duration of the callback.
  template <typename Fn>
  void decodePacket(const uint8_t *packet, Fn &&onMessage)
  {
    if (!packet)
    {
      return;
    }
    const uint8_t cable = usbPacketCable(packet);
    const UsbCin cin = usbPacketCin(packet);
    const uint8_t length = usbCinLength(cin);
    if (length == 0 || cable >= MaxPortsPerEndpoint)
    {
      return; // reserved CIN, or a cable outside the 4-bit range (unreachable)
    }
    CableState &state = state_[cable];

    // CIN 0x5 is shared between "SysEx ends with one byte" and a single-byte
    // System Common. Only an 0xF7 can end a SysEx, so the byte itself decides.
    const bool sysExEnd1 = cin == UsbCin::SysExEnd1 && packet[1] == 0xf7;
    if (!usbCinIsSysEx(cin) || (cin == UsbCin::SysExEnd1 && !sysExEnd1))
    {
      emitShort(state, packet, onMessage);
      return;
    }

    // A SysEx packet. The 0xF0 and 0xF7 framing bytes are not part of the
    // payload (docs/DATA_MODEL.ja.md), so the chunk points past them; because a
    // data byte can never be 0xF0 or 0xF7, testing the bytes is unambiguous.
    size_t begin = 1;
    size_t end = 1 + length;
    if (!state.inSysEx)
    {
      if (packet[1] != 0xf0)
      {
        // A continuation for a stream this decoder never saw start, which
        // happens after a reset in the middle of a dump. There is no start to
        // report, so the bytes are dropped rather than emitted as a chunk with
        // no beginning.
        return;
      }
      state.inSysEx = true;
      state.pendingChunkStart = true;
      begin = 2;
    }
    const bool end0xf7 = end > begin && packet[end - 1] == 0xf7;
    if (end0xf7)
    {
      end--;
    }
    const bool streamEnd = cin != UsbCin::SysExStart;

    Message message;
    message.port = state.port;
    message.type = MessageType::Data7;
    message.status = 0xf0;
    message.chunk = true;
    message.chunkStart = state.pendingChunkStart;
    message.chunkEnd = streamEnd;
    message.chunkData = end > begin ? &packet[begin] : nullptr;
    message.chunkLength = end > begin ? end - begin : 0;
    state.pendingChunkStart = false;
    if (streamEnd)
    {
      state.inSysEx = false;
    }
    onMessage(static_cast<const Message &>(message));
  }

  // Decodes a bulk transfer, which carries a whole number of packets. A trailing
  // partial packet is ignored.
  template <typename Fn>
  void decode(const uint8_t *data, size_t length, Fn &&onMessage)
  {
    if (!data)
    {
      return;
    }
    for (size_t offset = 0; offset + UsbPacketBytes <= length; offset += UsbPacketBytes)
    {
      decodePacket(&data[offset], onMessage);
    }
  }

private:
  struct CableState
  {
    PortId port;
    bool inSysEx = false;
    bool pendingChunkStart = false;
  };

  template <typename Fn>
  void emitShort(const CableState &state, const uint8_t *packet, Fn &&onMessage)
  {
    const uint8_t status = packet[1];
    const int dataLength = messageDataLength(status);
    if (dataLength < 0)
    {
      return; // not a message a receiver can act on
    }

    Message message;
    message.port = state.port;
    message.type = messageTypeForStatus(status);
    message.status = status;
    message.data1 = dataLength >= 1 ? packet[2] : 0;
    message.data2 = dataLength >= 2 ? packet[3] : 0;
    message.dataLength = static_cast<uint8_t>(dataLength);
    message.raw = &packet[1];
    message.length = static_cast<size_t>(dataLength) + 1;
    onMessage(static_cast<const Message &>(message));
  }

  CableState state_[MaxPortsPerEndpoint];
};

// --- Encoding -------------------------------------------------------------

// Messages to packets, for one cable. A SysEx chunk rarely divides into threes,
// so the leftover bytes are held here until the next chunk or the end of the
// stream completes a packet. Routing guarantees one open stream per output port
// at a time (docs/ROUTING.ja.md, rule 3), which is what makes one pending buffer
// per cable enough.
class UsbPacketEncoder
{
public:
  explicit UsbPacketEncoder(uint8_t cable = 0) : cable_(static_cast<uint8_t>(cable & 0x0f)) {}

  uint8_t cable() const { return cable_; }
  void setCable(uint8_t cable) { cable_ = static_cast<uint8_t>(cable & 0x0f); }

  // Drops a partially encoded SysEx. Nothing is flushed: a truncated dump is
  // closed by routing sending an ending chunk, not by the encoder inventing one.
  void reset()
  {
    pendingCount_ = 0;
    inSysEx_ = false;
  }

  bool inSysEx() const { return inSysEx_; }

  // Bytes encode() may write for a message, so a caller can size a buffer.
  static size_t maxEncodedBytes(const Message &message)
  {
    if (!message.chunk)
    {
      return UsbPacketBytes;
    }
    // Up to two bytes may already be pending, and the 0xF0 / 0xF7 framing adds
    // one at each end.
    const size_t bytes = message.chunkLength + 4;
    return ((bytes + 2) / 3) * UsbPacketBytes;
  }

  // Encodes a message into `dst` and returns the bytes written, always a
  // multiple of four. Returns 0 if the message cannot be encoded or `capacity`
  // is smaller than maxEncodedBytes().
  size_t encode(const Message &message, uint8_t *dst, size_t capacity)
  {
    if (!dst || capacity < maxEncodedBytes(message))
    {
      return 0;
    }
    return message.chunk ? encodeChunk(message, dst) : encodeShort(message, dst);
  }

private:
  size_t encodeShort(const Message &message, uint8_t *dst)
  {
    const int dataLength = messageDataLength(message.status);
    if (dataLength < 0)
    {
      return 0;
    }
    dst[0] = header(usbCinForStatus(message.status));
    dst[1] = message.status;
    dst[2] = dataLength >= 1 ? static_cast<uint8_t>(message.data1 & 0x7f) : 0;
    dst[3] = dataLength >= 2 ? static_cast<uint8_t>(message.data2 & 0x7f) : 0;
    return UsbPacketBytes;
  }

  size_t encodeChunk(const Message &message, uint8_t *dst)
  {
    size_t written = 0;

    if (message.chunkStart)
    {
      // A start while a stream is open would interleave two dumps on one cable;
      // routing prevents it, and the previous stream's leftovers are dropped
      // rather than merged into this one.
      pendingCount_ = 0;
      inSysEx_ = true;
      push(0xf0, dst, written);
    }
    else if (!inSysEx_)
    {
      return 0; // a continuation with no stream open
    }

    for (size_t i = 0; i < message.chunkLength; i++)
    {
      push(static_cast<uint8_t>(message.chunkData[i] & 0x7f), dst, written);
    }

    if (message.chunkEnd)
    {
      // Appended without going through push(): the terminator must stay in the
      // pending buffer so the CIN below can say how many bytes the last packet
      // carries. Flushing it as a full group would emit an empty extra packet.
      pending_[pendingCount_++] = 0xf7;
      // pendingCount_ was 0..2, so it is now 1..3 — exactly what the three
      // SysExEnd CINs describe.
      static const UsbCin endCin[4] = {
          UsbCin::SysExEnd1, // unreachable, the terminator was just appended
          UsbCin::SysExEnd1,
          UsbCin::SysExEnd2,
          UsbCin::SysExEnd3,
      };
      dst[written + 0] = header(endCin[pendingCount_]);
      dst[written + 1] = pendingCount_ > 0 ? pending_[0] : 0;
      dst[written + 2] = pendingCount_ > 1 ? pending_[1] : 0;
      dst[written + 3] = pendingCount_ > 2 ? pending_[2] : 0;
      written += UsbPacketBytes;
      pendingCount_ = 0;
      inSysEx_ = false;
    }

    return written;
  }

  void push(uint8_t byte, uint8_t *dst, size_t &written)
  {
    pending_[pendingCount_++] = byte;
    if (pendingCount_ == 3)
    {
      dst[written + 0] = header(UsbCin::SysExStart);
      dst[written + 1] = pending_[0];
      dst[written + 2] = pending_[1];
      dst[written + 3] = pending_[2];
      written += UsbPacketBytes;
      pendingCount_ = 0;
    }
  }

  uint8_t header(UsbCin cin) const
  {
    return static_cast<uint8_t>((cable_ << 4) | static_cast<uint8_t>(cin));
  }

  uint8_t cable_ = 0;
  uint8_t pending_[3] = {};
  uint8_t pendingCount_ = 0;
  bool inSysEx_ = false;
};

} // namespace espmidi

#endif // ESPMIDI_USB_PACKET_H
