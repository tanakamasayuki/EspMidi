// MIDI 1.0 byte stream ⇄ espmidi::Message.
//
// Parser is the receiving half, Serializer the sending one. Both live here for
// the same reason the USB packet codec keeps its decoder and encoder together:
// they are two readings of one wire format, and a change to one is nearly always
// a change to the other.
//
// This is what a byte-stream transport needs: UART today, and anything else that
// delivers a raw MIDI 1.0 stream later. USB does not use it — a USB MIDI event
// packet already carries the message boundary in its Code Index Number, so it
// has its own codec in EspMidiUsbPacket.h.
//
// The parser is pure C++ with no buffering of its own beyond three bytes, and it
// hands SysEx through without copying: a chunk points straight into the caller's
// input buffer. That is the whole point of the pointer lifetime rule in
// docs/DATA_MODEL.ja.md — a patch dump can cross the library without ever being
// duplicated.
//
// Three details of the MIDI 1.0 stream drive the shape of the state machine:
//
//   * Running status. A channel voice message may omit its status byte and reuse
//     the previous one, so the parser remembers it and resolves it before the
//     message is handed over. A consumer never sees a message without a status.
//   * System Real-Time (0xF8..0xFF) may appear between any two bytes, including
//     inside a SysEx, and must not disturb what it interrupts.
//   * A status byte other than System Real-Time terminates an open SysEx.

#ifndef ESPMIDI_PARSER_H
#define ESPMIDI_PARSER_H

#include "EspMidiMessage.h"

namespace espmidi
{

class Parser
{
public:
  Parser() = default;
  explicit Parser(PortId port) : port_(port) {}

  // The port stamped onto every message this parser emits.
  void setPort(PortId port) { port_ = port; }
  PortId port() const { return port_; }

  // Drops any partially received message and closes an open SysEx without
  // emitting anything. A port calls this when its link goes away, so bytes from
  // before a disconnect cannot combine with bytes from after it.
  void reset()
  {
    runningStatus_ = 0;
    status_ = 0;
    expected_ = 0;
    dataCount_ = 0;
    inSysEx_ = false;
    pendingChunkStart_ = false;
  }

  // True while a SysEx stream is open, i.e. between an 0xF0 and whatever ends
  // it. A port checks this on disconnect to decide whether downstream needs an
  // 0xF7 to close what it was already sending (docs/ROUTING.ja.md, rule 2).
  bool inSysEx() const { return inSysEx_; }

  // Feeds bytes and calls onMessage(const espmidi::Message &) once per complete
  // message. Pointers inside the message are valid only for that call.
  template <typename Fn>
  void parse(const uint8_t *data, size_t length, Fn &&onMessage)
  {
    if (!data)
    {
      return;
    }

    // A SysEx chunk has to be contiguous in memory to be handed over without
    // copying, so a run of data bytes is accumulated and flushed whenever
    // something interrupts it or the buffer ends.
    size_t runStart = 0;
    bool runActive = false;

    for (size_t i = 0; i < length; i++)
    {
      const uint8_t byte = data[i];

      // Checked first, and before any state is touched: a real-time byte is
      // allowed anywhere and changes nothing it interrupts.
      if (isSystemRealTime(byte))
      {
        if (runActive)
        {
          emitChunk(&data[runStart], i - runStart, false, onMessage);
          runActive = false;
        }
        emitShort(byte, 0, 0, 0, onMessage);
        continue;
      }

      if (inSysEx_)
      {
        if (isDataByte(byte))
        {
          if (!runActive)
          {
            runStart = i;
            runActive = true;
          }
          continue;
        }

        // Any status byte ends the stream, not just 0xF7. A device that stops
        // mid-dump and starts sending notes leaves a truncated SysEx, and the
        // chunk is closed so a downstream port can finish what it started
        // rather than wait for an 0xF7 that is never coming.
        if (runActive)
        {
          emitChunk(&data[runStart], i - runStart, true, onMessage);
          runActive = false;
        }
        else
        {
          emitChunk(nullptr, 0, true, onMessage);
        }
        inSysEx_ = false;

        if (isSysExEnd(byte))
        {
          // 0xF7 is the terminator itself and carries nothing further. It is a
          // System Common status, so it also cancels running status.
          runningStatus_ = 0;
          status_ = 0;
          dataCount_ = 0;
          continue;
        }
        // Anything else is a real message and is handled below.
      }

      handleByte(byte, onMessage);
    }

    if (runActive)
    {
      emitChunk(&data[runStart], length - runStart, false, onMessage);
    }
  }

private:
  template <typename Fn>
  void handleByte(uint8_t byte, Fn &&onMessage)
  {
    if (isStatusByte(byte))
    {
      if (isSysExStart(byte))
      {
        inSysEx_ = true;
        pendingChunkStart_ = true;
        runningStatus_ = 0;
        status_ = 0;
        dataCount_ = 0;
        return;
      }
      if (isSysExEnd(byte))
      {
        // An 0xF7 with no stream open. Nothing to emit, but it is a System
        // Common status and cancels running status.
        runningStatus_ = 0;
        status_ = 0;
        dataCount_ = 0;
        return;
      }

      const int dataLength = messageDataLength(byte);
      if (dataLength < 0)
      {
        // 0xF4 / 0xF5 are undefined. The specification requires a receiver to
        // ignore them, and they cancel running status like any System Common.
        runningStatus_ = 0;
        status_ = 0;
        dataCount_ = 0;
        return;
      }

      status_ = byte;
      expected_ = static_cast<uint8_t>(dataLength);
      dataCount_ = 0;
      // Only channel voice messages may be repeated without their status byte.
      runningStatus_ = isChannelVoice(byte) ? byte : 0;

      if (expected_ == 0)
      {
        // Tune Request, and nothing else that reaches here.
        emitShort(status_, 0, 0, 0, onMessage);
        status_ = 0;
      }
      return;
    }

    // A data byte. With no message in progress it belongs to the running status,
    // if there is one; otherwise it is an orphan and is dropped.
    if (status_ == 0)
    {
      if (runningStatus_ == 0)
      {
        return;
      }
      status_ = runningStatus_;
      expected_ = static_cast<uint8_t>(messageDataLength(status_));
      dataCount_ = 0;
    }

    if (dataCount_ < 2)
    {
      data_[dataCount_] = byte;
    }
    dataCount_++;

    if (dataCount_ >= expected_)
    {
      emitShort(status_, data_[0], data_[1], expected_, onMessage);
      // Cleared unconditionally: running status is restored from
      // runningStatus_, which is already 0 for the messages that cancel it.
      status_ = 0;
      dataCount_ = 0;
    }
  }

  template <typename Fn>
  void emitShort(uint8_t status, uint8_t data1, uint8_t data2, uint8_t dataLength, Fn &&onMessage)
  {
    assembled_[0] = status;
    assembled_[1] = data1;
    assembled_[2] = data2;

    Message message;
    message.port = port_;
    message.type = messageTypeForStatus(status);
    message.status = status;
    message.data1 = dataLength >= 1 ? data1 : 0;
    message.data2 = dataLength >= 2 ? data2 : 0;
    message.dataLength = dataLength;
    message.raw = assembled_;
    message.length = static_cast<size_t>(dataLength) + 1;
    onMessage(static_cast<const Message &>(message));
  }

  template <typename Fn>
  void emitChunk(const uint8_t *chunkData, size_t chunkLength, bool end, Fn &&onMessage)
  {
    Message message;
    message.port = port_;
    message.type = MessageType::Data7;
    message.status = 0xf0;
    message.dataLength = 0;
    message.chunk = true;
    message.chunkStart = pendingChunkStart_;
    message.chunkEnd = end;
    message.chunkData = chunkData;
    message.chunkLength = chunkLength;
    pendingChunkStart_ = false;
    onMessage(static_cast<const Message &>(message));
  }

  PortId port_;
  uint8_t runningStatus_ = 0;
  uint8_t status_ = 0;
  uint8_t expected_ = 0;
  uint8_t dataCount_ = 0;
  uint8_t data_[2] = {};
  uint8_t assembled_[MaxShortMessageBytes] = {};
  bool inSysEx_ = false;
  bool pendingChunkStart_ = false;
};

// espmidi::Message to a MIDI 1.0 byte stream.
//
// The framing bytes of a data stream are the serializer's job, not the queue's:
// a chunk carries only its payload, so 0xF0 goes in front of the first chunk and
// 0xF7 after the last one (docs/DATA_MODEL.ja.md). Everything between is handed
// to the transport exactly as it arrived, which is what keeps a patch dump from
// being copied on its way out.
//
// Bytes are written through a callback rather than into a buffer so the payload
// never has to be staged anywhere: the caller writes the chunk straight from the
// pointer it was given.
//
// Running status is not used on the way out. It would save one byte in three on
// a stream of notes, but an output port carries messages that arrived from
// several inputs, and a receiver that loses one byte of a compressed stream
// misreads everything after it rather than one message. The saving is not worth
// that at 31250 baud.
class Serializer
{
public:
  // Forgets an open stream without closing it. A port calls this when its link
  // goes away and the bytes could not be sent anyway.
  void reset() { open_ = false; }

  // True while a data stream has been started but not finished, i.e. an 0xF7 is
  // still owed to whatever is listening.
  bool inStream() const { return open_; }

  // Writes one message. `write(const uint8_t *, size_t)` returns false if the
  // transport refused the bytes; serialize() then stops and returns false.
  //
  // Returns false for a message it cannot express — a status byte with no fixed
  // length, or a continuation chunk for a stream that was never started.
  template <typename Fn>
  bool serialize(const Message &message, Fn &&write)
  {
    if (!message.chunk)
    {
      uint8_t bytes[MaxShortMessageBytes] = {};
      const size_t length = serializeShortMessage(message, bytes, sizeof(bytes));
      if (length == 0)
      {
        return false;
      }
      return write(static_cast<const uint8_t *>(bytes), length);
    }

    if (message.chunkStart)
    {
      static const uint8_t start = 0xf0;
      if (!write(&start, static_cast<size_t>(1)))
      {
        return false;
      }
      open_ = true;
    }
    else if (!open_)
    {
      // A continuation with no beginning. Emitting the payload alone would put
      // loose data bytes on the wire, which a receiver resolves against whatever
      // running status it happens to hold.
      return false;
    }

    if (message.chunkLength > 0 && message.chunkData)
    {
      if (!write(message.chunkData, message.chunkLength))
      {
        open_ = false;
        return false;
      }
    }

    if (message.chunkEnd)
    {
      open_ = false;
      static const uint8_t end = 0xf7;
      if (!write(&end, static_cast<size_t>(1)))
      {
        return false;
      }
    }
    return true;
  }

  // Ends an open stream with an 0xF7 (docs/ROUTING.ja.md, rule 2). A port calls
  // this when the source of a stream disappears mid-dump, so the device on the
  // other end is not left waiting for a terminator that is never coming.
  template <typename Fn>
  bool closeStream(Fn &&write)
  {
    if (!open_)
    {
      return true;
    }
    open_ = false;
    static const uint8_t end = 0xf7;
    return write(&end, static_cast<size_t>(1));
  }

private:
  bool open_ = false;
};

} // namespace espmidi

#endif // ESPMIDI_PARSER_H
