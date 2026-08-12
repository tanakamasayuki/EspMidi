// Declarative filtering and transformation.
//
// Specification: docs/ROUTING.ja.md. These are the rules a sketch states as
// data rather than as code — "only channel 1", "transpose up an octave", "scale
// the volume pedal to half". User code that has to look at a message and decide
// goes in a callback stage instead, and anything that has to emit a different
// message goes through an application port.
//
// One thing here is shaped by MIDI 2.0 rather than by MIDI 1.0. Note numbers,
// channels and controller numbers are the same width in both, so they are plain
// integers. Velocities and controller values are not: 7 bits today, 16 and 32
// bits under MIDI 2.0. Those go through ValueMap, which stores its endpoints
// normalised rather than in wire units, so a map written today keeps meaning the
// same thing when a wider version of from()/to() is added
// (docs/DECISIONS.ja.md, decision 1).

#ifndef ESPMIDI_FILTER_H
#define ESPMIDI_FILTER_H

#include "EspMidiMessage.h"

namespace espmidi
{

// What a message is, as a bit so a filter can name a set of them.
enum MessageKind : uint16_t
{
  KindNoteOff = 1u << 0,
  KindNoteOn = 1u << 1,
  KindPolyPressure = 1u << 2,
  KindControlChange = 1u << 3,
  KindProgramChange = 1u << 4,
  KindChannelPressure = 1u << 5,
  KindPitchBend = 1u << 6,
  KindSystemCommon = 1u << 7,
  KindSystemRealTime = 1u << 8,
  KindData = 1u << 9, // SysEx and any other data stream

  KindNotes = KindNoteOff | KindNoteOn,
  KindChannelVoice = KindNoteOff | KindNoteOn | KindPolyPressure | KindControlChange |
                     KindProgramChange | KindChannelPressure | KindPitchBend,
  KindSystem = KindSystemCommon | KindSystemRealTime,
  KindAll = 0xffff,
};

inline uint16_t messageKind(const Message &message)
{
  if (message.chunk)
  {
    return KindData;
  }
  if (isChannelVoice(message.status))
  {
    switch (message.status & 0xf0)
    {
    case 0x80:
      return KindNoteOff;
    case 0x90:
      // A note on with velocity 0 is a note off on the wire. Treating it as a
      // note on would let "block note off" leak the release through.
      return message.data2 == 0 ? KindNoteOff : KindNoteOn;
    case 0xa0:
      return KindPolyPressure;
    case 0xb0:
      return KindControlChange;
    case 0xc0:
      return KindProgramChange;
    case 0xd0:
      return KindChannelPressure;
    default:
      return KindPitchBend;
    }
  }
  return isSystemRealTime(message.status) ? KindSystemRealTime : KindSystemCommon;
}

// --- Values ---------------------------------------------------------------

// A velocity or controller value, held normalised to 16 bits so the rule does
// not depend on the wire resolution. MIDI 1.0 supplies 7 bits; MIDI 2.0 will
// supply 16 or 32, and only the conversion helpers change.
inline uint16_t normalizeFrom7(uint8_t value)
{
  return static_cast<uint16_t>((static_cast<uint32_t>(value & 0x7f) * 65535u) / 127u);
}

inline uint8_t denormalizeTo7(uint16_t value)
{
  return static_cast<uint8_t>((static_cast<uint32_t>(value) * 127u + 32767u) / 65535u);
}

// Maps an input value onto an output range, clamping the input first. Identity
// until it is given a range, so a Transform with nothing set changes nothing.
struct ValueMap
{
  bool enabled = false;
  uint16_t inMin = 0;
  uint16_t inMax = 65535;
  uint16_t outMin = 0;
  uint16_t outMax = 65535;

  // Endpoints given in MIDI 1.0 units. A wider from16()/from32() can be added
  // later without changing what an existing map means.
  static ValueMap range7(uint8_t inLow, uint8_t inHigh, uint8_t outLow, uint8_t outHigh)
  {
    ValueMap map;
    map.enabled = true;
    map.inMin = normalizeFrom7(inLow);
    map.inMax = normalizeFrom7(inHigh);
    map.outMin = normalizeFrom7(outLow);
    map.outMax = normalizeFrom7(outHigh);
    return map;
  }

  // The whole input range onto a narrower output: a volume limiter.
  static ValueMap scale7(uint8_t outLow, uint8_t outHigh)
  {
    return range7(0, 127, outLow, outHigh);
  }

  // Every input to one value.
  static ValueMap fixed7(uint8_t value) { return range7(0, 127, value, value); }

  uint16_t apply(uint16_t value) const
  {
    if (!enabled)
    {
      return value;
    }
    if (inMax == inMin)
    {
      return outMin;
    }
    const bool ascending = inMax > inMin;
    const uint16_t low = ascending ? inMin : inMax;
    const uint16_t high = ascending ? inMax : inMin;
    uint16_t clamped = value;
    if (clamped < low)
    {
      clamped = low;
    }
    else if (clamped > high)
    {
      clamped = high;
    }

    const uint32_t span = static_cast<uint32_t>(high) - low;
    const uint32_t position = static_cast<uint32_t>(clamped) - low;
    // Written so an inverted output range (outMin > outMax) works too, which is
    // how a pedal gets reversed.
    if (outMax >= outMin)
    {
      const uint32_t outSpan = static_cast<uint32_t>(outMax) - outMin;
      return static_cast<uint16_t>(outMin + (position * outSpan + span / 2) / span);
    }
    const uint32_t outSpan = static_cast<uint32_t>(outMin) - outMax;
    return static_cast<uint16_t>(outMin - (position * outSpan + span / 2) / span);
  }

  uint8_t apply7(uint8_t value) const
  {
    return enabled ? denormalizeTo7(apply(normalizeFrom7(value))) : value;
  }
};

// --- Filter ---------------------------------------------------------------

// Which messages a stage lets through. Everything passes by default, so a filter
// only ever narrows.
struct Filter
{
  uint16_t kinds = KindAll;
  // One bit per MIDI channel. Only consulted for channel voice messages.
  uint16_t channels = 0xffff;
  // Note range, for note on / note off / polyphonic pressure.
  uint8_t noteMin = 0;
  uint8_t noteMax = 127;
  // Controller number range, for control change.
  uint8_t ccMin = 0;
  uint8_t ccMax = 127;

  void allowOnlyChannel(uint8_t channel) { channels = static_cast<uint16_t>(1u << (channel & 0x0f)); }
  void allowChannel(uint8_t channel) { channels |= static_cast<uint16_t>(1u << (channel & 0x0f)); }
  void blockChannel(uint8_t channel) { channels &= static_cast<uint16_t>(~(1u << (channel & 0x0f))); }

  bool accepts(const Message &message) const
  {
    const uint16_t kind = messageKind(message);
    if ((kinds & kind) == 0)
    {
      return false;
    }
    if (!isChannelVoice(message.status) || message.chunk)
    {
      return true; // system messages and data streams have no channel or note
    }
    if ((channels & (1u << message.channel())) == 0)
    {
      return false;
    }
    if ((kind & (KindNotes | KindPolyPressure)) != 0)
    {
      return message.data1 >= noteMin && message.data1 <= noteMax;
    }
    if ((kind & KindControlChange) != 0)
    {
      return message.data1 >= ccMin && message.data1 <= ccMax;
    }
    return true;
  }
};

// --- Transform ------------------------------------------------------------

// How a stage rewrites what passes its filter. Everything is "keep" by default.
//
// Narrowing is the filter's job, not this one's: "CC 7 becomes CC 11" is a route
// filtered to CC 7 with controller set to 11, which is why there is no remapping
// table here.
struct Transform
{
  // 0..15 to set the channel, or negative to keep it.
  int8_t channel = -1;
  // Added to the channel afterwards, wrapping within the 16 channels.
  int8_t channelOffset = 0;

  // Semitones added to note on / note off / polyphonic pressure. A note pushed
  // outside 0..127 is dropped rather than wrapped, because wrapping would put a
  // note at the wrong end of the keyboard.
  int16_t transpose = 0;
  // Added to the note number of the same messages, kept separate from transpose
  // so "this device is one octave down" and "this route transposes" compose.
  int16_t noteOffset = 0;

  // 0..127 to set the controller number, or negative to keep it.
  int16_t controller = -1;

  // Note on velocity, and note off release velocity.
  ValueMap velocity;
  // Control change value.
  ValueMap controllerValue;
  // Polyphonic and channel pressure.
  ValueMap pressure;

  // Rewrites the message in place. Returns false when the result cannot be
  // represented and the message has to be dropped.
  bool apply(Message &message) const
  {
    if (message.chunk || !isChannelVoice(message.status))
    {
      return true; // nothing here applies to data streams or system messages
    }

    if (channel >= 0 || channelOffset != 0)
    {
      int value = channel >= 0 ? channel : message.channel();
      value = (value + channelOffset) & 0x0f;
      message.status = static_cast<uint8_t>((message.status & 0xf0) | value);
    }

    const uint16_t kind = messageKind(message);

    if ((kind & (KindNotes | KindPolyPressure)) != 0)
    {
      const int shifted = static_cast<int>(message.data1) + transpose + noteOffset;
      if (shifted < 0 || shifted > 127)
      {
        return false;
      }
      message.data1 = static_cast<uint8_t>(shifted);
    }

    if ((kind & KindNotes) != 0)
    {
      // A note on with velocity 0 means note off. Scaling it would turn a
      // release into a very quiet note, so the zero is left alone.
      if (message.data2 != 0)
      {
        message.data2 = velocity.apply7(message.data2);
      }
    }
    else if ((kind & KindControlChange) != 0)
    {
      if (controller >= 0)
      {
        message.data1 = static_cast<uint8_t>(controller & 0x7f);
      }
      message.data2 = controllerValue.apply7(message.data2);
    }
    else if ((kind & KindPolyPressure) != 0)
    {
      message.data2 = pressure.apply7(message.data2);
    }
    else if ((kind & KindChannelPressure) != 0)
    {
      message.data1 = pressure.apply7(message.data1);
    }

    return true;
  }
};

} // namespace espmidi

#endif // ESPMIDI_FILTER_H
