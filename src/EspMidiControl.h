// Control mapping: knobs, buttons, encoders and clock, as MIDI.
//
// Specification: docs/REQUIREMENTS.ja.md (the control mapping use cases).
//
// These are the pieces that turn a board into a MIDI controller rather than a
// MIDI router. They all sit on an application port, so what they produce is
// routed exactly like a UART or a USB cable: filtered, transformed, sent to
// several destinations, or watched by the sketch.
//
// Two rules shape every helper here.
//
// **No GPIO.** Nothing in this file reads a pin, and no helper is told a pin
// number. The sketch reads whatever it has — an ADC, a port expander, a touch
// sensor, a value off a network — and hands the number over. A helper that owned
// analogRead() would be a helper that only worked for one kind of input.
//
// **No clock.** Time is a parameter, never a call to millis(). That keeps the
// whole file portable C++ that tests can run at any speed, forwards and
// backwards, which is the only practical way to test debouncing and tempo.
//
// The consequence is that a helper is a small state machine with one entry point:
// give it the current reading and the current time, and it decides whether
// something should be sent.

#ifndef ESPMIDI_CONTROL_H
#define ESPMIDI_CONTROL_H

#include "EspMidiFilter.h"
#include "EspMidiMessage.h"
#include "EspMidiRouter.h"

namespace espmidi
{

// --- Button ---------------------------------------------------------------

struct ButtonConfig
{
  uint8_t channel = 0;
  // A note, or a control change. A footswitch into a sound module is usually a
  // note; a footswitch into a DAW is usually a control change.
  bool note = true;
  uint8_t number = 60; // note number, or controller number
  uint8_t onValue = 127;
  uint8_t offValue = 0;
  // A change is believed only after the input has been still for this long. Real
  // switches bounce for a few milliseconds and would otherwise produce a burst
  // of notes on every press.
  uint16_t debounceMs = 20;
  // Momentary by default: pressed sends on, released sends off. Latching turns a
  // momentary switch into a toggle, which is what a switch controlling a mode
  // wants.
  bool latch = false;
};

class Button
{
public:
  explicit Button(AppPort &port, const ButtonConfig &config = ButtonConfig()) : port_(port), config_(config) {}

  ButtonConfig &config() { return config_; }
  const ButtonConfig &config() const { return config_; }

  // True while the button is logically on, which for a latching button is not the
  // same as being held down.
  bool on() const { return on_; }

  // Hands over the current reading. Returns true if something was sent.
  bool update(bool pressed, uint32_t nowMs)
  {
    if (!seen_)
    {
      // The first reading establishes the resting state without sending
      // anything: a board that starts up with a pedal already down should not
      // announce a note nobody played.
      seen_ = true;
      pressed_ = pressed;
      changedMs_ = nowMs;
      return false;
    }

    if (pressed == pressed_)
    {
      return false;
    }
    if (static_cast<uint32_t>(nowMs - changedMs_) < config_.debounceMs)
    {
      return false;
    }

    pressed_ = pressed;
    changedMs_ = nowMs;

    if (config_.latch)
    {
      if (!pressed)
      {
        return false; // a latching button acts on the press only
      }
      on_ = !on_;
    }
    else
    {
      on_ = pressed;
    }
    return send(on_);
  }

  // Sends the current state again, for a controller that has just been asked to
  // resend everything.
  bool resend() { return send(on_); }

private:
  bool send(bool state)
  {
    const uint8_t value = state ? config_.onValue : config_.offValue;
    if (config_.note)
    {
      // Note off rather than a note on with velocity 0: both stop the note, and
      // the explicit one is what a receiver with release velocity expects.
      const uint8_t status = static_cast<uint8_t>((state ? 0x90 : 0x80) | (config_.channel & 0x0f));
      return port_.sendShort(status, config_.number, value);
    }
    return port_.sendShort(static_cast<uint8_t>(0xb0 | (config_.channel & 0x0f)), config_.number, value);
  }

  AppPort &port_;
  ButtonConfig config_;
  uint32_t changedMs_ = 0;
  bool pressed_ = false;
  bool on_ = false;
  bool seen_ = false;
};

// --- Analog input ---------------------------------------------------------

struct AnalogConfig
{
  uint8_t channel = 0;
  uint8_t controller = 7;
  // The reading's own range, in whatever units the sketch read it in. Swapping
  // the two reverses the control, which is how a pedal wired backwards is fixed
  // without rewiring it.
  uint16_t rawMin = 0;
  uint16_t rawMax = 4095;
  uint8_t outLow = 0;
  uint8_t outHigh = 127;
  // Raw units the reading must move before it is believed. An ADC on a long wire
  // wanders by a few counts, and without this the knob sends a stream of control
  // changes while nobody is touching it.
  uint16_t hysteresis = 8;
  // Exponential smoothing as a shift: 0 is off, 1 halves each step's effect, 2
  // quarters it. Smoothing adds lag, so it is off by default.
  uint8_t smoothing = 0;
};

class Analog
{
public:
  explicit Analog(AppPort &port, const AnalogConfig &config = AnalogConfig()) : port_(port), config_(config) {}

  AnalogConfig &config() { return config_; }
  const AnalogConfig &config() const { return config_; }

  uint8_t value() const { return value_; }
  uint16_t raw() const { return filtered_; }

  // Hands over the current reading. Returns true if a control change was sent,
  // which happens only when the mapped value actually changed.
  bool update(uint16_t raw)
  {
    if (!seen_)
    {
      seen_ = true;
      filtered_ = raw;
      accepted_ = raw;
      value_ = map(raw);
      // The first reading is where the knob already is. Sending it would be
      // right for a controller that wants to announce itself, so it is offered
      // through resend() rather than done here.
      return false;
    }

    if (config_.smoothing > 0)
    {
      const int32_t delta = static_cast<int32_t>(raw) - static_cast<int32_t>(filtered_);
      filtered_ = static_cast<uint16_t>(static_cast<int32_t>(filtered_) + (delta >> config_.smoothing));
      // A shift alone never quite arrives, so the last count is stepped over.
      if (delta != 0 && filtered_ == accepted_ && (delta > 0) == (raw > filtered_))
      {
        filtered_ = static_cast<uint16_t>(static_cast<int32_t>(filtered_) + (delta > 0 ? 1 : -1));
      }
    }
    else
    {
      filtered_ = raw;
    }

    const uint16_t moved = filtered_ > accepted_ ? filtered_ - accepted_ : accepted_ - filtered_;
    if (moved < config_.hysteresis)
    {
      return false;
    }
    accepted_ = filtered_;

    const uint8_t mapped = map(filtered_);
    if (mapped == value_)
    {
      return false;
    }
    value_ = mapped;
    return sendValue();
  }

  // Sends where the knob is now, whether or not it moved.
  bool resend() { return sendValue(); }

private:
  // Raw units to 7 bits. Written so either range can be inverted, which is how a
  // pedal wired backwards or a control meant to fall as it is turned up is fixed
  // in configuration rather than in wiring.
  uint8_t map(uint16_t raw) const
  {
    const bool ascending = config_.rawMax >= config_.rawMin;
    const uint16_t low = ascending ? config_.rawMin : config_.rawMax;
    const uint16_t high = ascending ? config_.rawMax : config_.rawMin;
    if (high == low)
    {
      return static_cast<uint8_t>(config_.outLow & 0x7f);
    }

    uint16_t clamped = raw < low ? low : (raw > high ? high : raw);
    // Measured from the end the configuration calls the bottom, so swapping
    // rawMin and rawMax reverses the control.
    const uint32_t position = ascending ? static_cast<uint32_t>(clamped) - low
                                        : static_cast<uint32_t>(high) - clamped;
    const uint32_t span = static_cast<uint32_t>(high) - low;

    const uint8_t outLow = static_cast<uint8_t>(config_.outLow & 0x7f);
    const uint8_t outHigh = static_cast<uint8_t>(config_.outHigh & 0x7f);
    if (outHigh >= outLow)
    {
      const uint32_t outSpan = static_cast<uint32_t>(outHigh) - outLow;
      return static_cast<uint8_t>(outLow + (position * outSpan + span / 2) / span);
    }
    const uint32_t outSpan = static_cast<uint32_t>(outLow) - outHigh;
    return static_cast<uint8_t>(outLow - (position * outSpan + span / 2) / span);
  }

  bool sendValue()
  {
    return port_.sendShort(static_cast<uint8_t>(0xb0 | (config_.channel & 0x0f)), config_.controller, value_);
  }

  AppPort &port_;
  AnalogConfig config_;
  uint16_t filtered_ = 0;
  uint16_t accepted_ = 0;
  uint8_t value_ = 0;
  bool seen_ = false;
};

// --- Encoder --------------------------------------------------------------

// How a turn is expressed. There is no standard, and a controller that picks the
// wrong one turns the parameter the wrong way or not at all, so all three of the
// conventions in common use are here.
enum class EncoderMode : uint8_t
{
  // The controller keeps the value and sends where it now is. Works everywhere,
  // but the value can disagree with what the receiver holds.
  Absolute = 0,
  // 1..63 for +1..+63, 127..65 for -1..-63.
  RelativeTwosComplement,
  // 65..127 for +1..+63, 1..63 for -1..-63.
  RelativeSignedBit,
  // 64 plus the delta: 65 is +1, 63 is -1.
  RelativeBinaryOffset,
};

struct EncoderConfig
{
  uint8_t channel = 0;
  uint8_t controller = 16;
  EncoderMode mode = EncoderMode::Absolute;
  // Value change per detent. A rotary with four counts per detent is handled by
  // the sketch dividing, or by a step of 1 and a coarser reading.
  int16_t step = 1;
  uint8_t value = 64; // where an absolute encoder starts
};

class Encoder
{
public:
  explicit Encoder(AppPort &port, const EncoderConfig &config = EncoderConfig())
      : port_(port), config_(config), value_(config.value)
  {
  }

  EncoderConfig &config() { return config_; }
  const EncoderConfig &config() const { return config_; }

  uint8_t value() const { return value_; }

  // Hands over the encoder's accumulated position. A position rather than a delta
  // because that is what a quadrature decoder or a hardware counter holds, and
  // deriving the delta here means a missed call loses nothing.
  bool update(int32_t position)
  {
    if (!seen_)
    {
      seen_ = true;
      position_ = position;
      return false;
    }
    const int32_t delta = position - position_;
    if (delta == 0)
    {
      return false;
    }
    position_ = position;
    return turn(delta);
  }

  // Reports a turn directly, for a decoder that produces deltas.
  bool turn(int32_t detents)
  {
    if (detents == 0)
    {
      return false;
    }
    int32_t change = detents * config_.step;

    if (config_.mode == EncoderMode::Absolute)
    {
      int32_t next = static_cast<int32_t>(value_) + change;
      next = next < 0 ? 0 : (next > 127 ? 127 : next);
      if (next == value_)
      {
        return false; // already at the end of its travel
      }
      value_ = static_cast<uint8_t>(next);
      return send(value_);
    }

    // Relative modes carry at most 63 per message, so a big jump goes out as
    // several. Clamping instead would lose part of a fast turn.
    bool sent = false;
    while (change != 0)
    {
      const int32_t take = change > 63 ? 63 : (change < -63 ? -63 : change);
      change -= take;
      sent = send(relative(static_cast<int8_t>(take))) || sent;
    }
    return sent;
  }

private:
  uint8_t relative(int8_t delta) const
  {
    switch (config_.mode)
    {
      case EncoderMode::RelativeTwosComplement:
        return delta >= 0 ? static_cast<uint8_t>(delta) : static_cast<uint8_t>(128 + delta);
      case EncoderMode::RelativeSignedBit:
        return delta >= 0 ? static_cast<uint8_t>(64 + delta) : static_cast<uint8_t>(-delta);
      case EncoderMode::RelativeBinaryOffset:
      default:
      {
        const int32_t value = 64 + delta;
        return static_cast<uint8_t>(value < 0 ? 0 : (value > 127 ? 127 : value));
      }
    }
  }

  bool send(uint8_t value)
  {
    return port_.sendShort(static_cast<uint8_t>(0xb0 | (config_.channel & 0x0f)), config_.controller, value);
  }

  AppPort &port_;
  EncoderConfig config_;
  int32_t position_ = 0;
  uint8_t value_ = 64;
  bool seen_ = false;
};

// --- Driving something from received MIDI ---------------------------------

// The level a message asks for, 0..127. A note off, and a note on with velocity
// 0, both ask for 0 — which is what makes an LED follow a keyboard correctly
// rather than latching on forever.
inline uint8_t messageLevel(const Message &message)
{
  const uint16_t kind = messageKind(message);
  if ((kind & KindNoteOff) != 0)
  {
    return 0;
  }
  if ((kind & KindNoteOn) != 0)
  {
    return message.data2;
  }
  if ((kind & (KindControlChange | KindPolyPressure)) != 0)
  {
    return message.data2;
  }
  if ((kind & KindChannelPressure) != 0)
  {
    return message.data1;
  }
  return 0;
}

// What an output binding drives: an LED, a relay, a motor, a display.
using LevelCallback = void (*)(void *context, uint8_t level, const Message &message);

// Turns received MIDI into a level. It is the mirror of Button and Analog, and it
// reuses Filter for the matching, so "note 60 on channel 1" and "any control
// change between 20 and 27" are written the same way as anywhere else.
class ControlOutput
{
public:
  ControlOutput() = default;
  ControlOutput(const Filter &filter, LevelCallback callback, void *context = nullptr)
      : filter_(filter), callback_(callback), context_(context)
  {
  }

  Filter &filter() { return filter_; }
  const Filter &filter() const { return filter_; }

  void onLevel(LevelCallback callback, void *context = nullptr)
  {
    callback_ = callback;
    context_ = context;
  }

  uint8_t level() const { return level_; }

  // Offers a message. Returns true if it matched and the callback ran.
  bool handle(const Message &message)
  {
    if (message.chunk || !callback_ || !filter_.accepts(message))
    {
      return false;
    }
    level_ = messageLevel(message);
    callback_(context_, level_, message);
    return true;
  }

  // The shape MessageCallback wants, so this can be attached to an application
  // port directly.
  static void receive(void *context, const Message &message)
  {
    static_cast<ControlOutput *>(context)->handle(message);
  }

private:
  Filter filter_;
  LevelCallback callback_ = nullptr;
  void *context_ = nullptr;
  uint8_t level_ = 0;
};

// --- Clock ----------------------------------------------------------------

// MIDI Clock is 24 ticks per quarter note, which is the one piece of arithmetic
// both directions share.
static constexpr uint8_t MidiClockTicksPerQuarter = 24;

// Microseconds between clock ticks at a tempo given in hundredths of a beat per
// minute. Hundredths rather than a float because the core has no floating point
// requirement and 0.01 BPM is finer than anything audible.
inline uint32_t microsPerClockTick(uint32_t bpmTimes100)
{
  if (bpmTimes100 == 0)
  {
    return 0;
  }
  // 60 seconds * 1e6 us * 100 / (bpm100 * 24)
  return static_cast<uint32_t>(250000000u / bpmTimes100);
}

inline uint32_t bpmTimes100FromTick(uint32_t microsPerTick)
{
  if (microsPerTick == 0)
  {
    return 0;
  }
  return static_cast<uint32_t>(250000000u / microsPerTick);
}

// Sends MIDI Clock, and the transport messages that go with it.
//
// Time is a parameter, so the sketch decides where it comes from: micros(), a
// hardware timer, or a measured external pulse. Feeding it the tempo a
// ClockCounter measured is how an external clock is regenerated.
class ClockGenerator
{
public:
  // Ticks emitted in one update(). After a stall — a long SysEx, a blocking
  // write — catching up completely would send a burst that means nothing
  // musically, so the schedule is resynchronised instead.
  static constexpr size_t MaxCatchUpTicks = MidiClockTicksPerQuarter;

  explicit ClockGenerator(AppPort &port) : port_(port) {}

  void setTempo(uint32_t bpmTimes100) { microsPerTick_ = microsPerClockTick(bpmTimes100); }
  void setMicrosPerTick(uint32_t micros) { microsPerTick_ = micros; }
  uint32_t microsPerTick() const { return microsPerTick_; }
  uint32_t bpmTimes100() const { return bpmTimes100FromTick(microsPerTick_); }
  bool running() const { return running_; }

  // 0xFA, and the first tick is due immediately.
  bool start(uint32_t nowMicros)
  {
    running_ = true;
    nextTickMicros_ = nowMicros;
    return port_.sendShort(0xfa);
  }

  // 0xFB: carry on from where the receiver's song position already is.
  bool resume(uint32_t nowMicros)
  {
    running_ = true;
    nextTickMicros_ = nowMicros;
    return port_.sendShort(0xfb);
  }

  bool stop()
  {
    running_ = false;
    return port_.sendShort(0xfc);
  }

  // Sends whatever ticks are due. Returns how many went out.
  size_t update(uint32_t nowMicros)
  {
    if (!running_ || microsPerTick_ == 0)
    {
      return 0;
    }
    size_t sent = 0;
    while (static_cast<int32_t>(nowMicros - nextTickMicros_) >= 0)
    {
      if (sent >= MaxCatchUpTicks)
      {
        nextTickMicros_ = nowMicros + microsPerTick_;
        break;
      }
      port_.sendShort(0xf8);
      nextTickMicros_ += microsPerTick_;
      sent++;
    }
    return sent;
  }

private:
  AppPort &port_;
  uint32_t microsPerTick_ = 0;
  uint32_t nextTickMicros_ = 0;
  bool running_ = false;
};

// Follows incoming MIDI Clock: where the beat is, and how fast it is going.
//
// This is the half that makes an external clock usable — measure it here, hand
// the tempo to a ClockGenerator, and the board is a clock converter.
class ClockCounter
{
public:
  // Ticks averaged for the tempo. A quarter note's worth smooths the jitter a
  // sender's own scheduling adds without making the reading slow to follow a
  // tempo change.
  static constexpr uint8_t AverageTicks = MidiClockTicksPerQuarter;

  bool running() const { return running_; }
  // Ticks since the last quarter note, 0..23.
  uint8_t tick() const { return tick_; }
  // Quarter notes since the last Start.
  uint32_t quarters() const { return quarters_; }
  uint32_t microsPerTick() const { return microsPerTick_; }
  uint32_t bpmTimes100() const { return bpmTimes100FromTick(microsPerTick_); }
  // True on the update that landed on a quarter note.
  bool onQuarter() const { return onQuarter_; }

  void reset()
  {
    running_ = false;
    tick_ = 0;
    quarters_ = 0;
    microsPerTick_ = 0;
    counted_ = 0;
    onQuarter_ = false;
  }

  // Offers a message. Returns true if it was a clock or transport message, i.e.
  // one this counter consumed.
  bool handle(const Message &message, uint32_t nowMicros)
  {
    onQuarter_ = false;
    switch (message.status)
    {
      case 0xfa: // Start
        running_ = true;
        tick_ = 0;
        quarters_ = 0;
        counted_ = 0;
        windowStartMicros_ = nowMicros;
        return true;
      case 0xfb: // Continue
        running_ = true;
        counted_ = 0;
        windowStartMicros_ = nowMicros;
        return true;
      case 0xfc: // Stop
        running_ = false;
        return true;
      case 0xf8:
        break;
      default:
        return false;
    }

    // A clock. Some senders never send Start, so the first tick starts the count.
    if (!running_)
    {
      running_ = true;
      windowStartMicros_ = nowMicros;
    }

    tick_++;
    if (tick_ >= MidiClockTicksPerQuarter)
    {
      tick_ = 0;
      quarters_++;
      onQuarter_ = true;
    }

    counted_++;
    if (counted_ >= AverageTicks)
    {
      const uint32_t elapsed = nowMicros - windowStartMicros_;
      microsPerTick_ = elapsed / counted_;
      counted_ = 0;
      windowStartMicros_ = nowMicros;
    }
    return true;
  }

  // The shape MessageCallback wants is not enough here, because the time has to
  // come with it. A sketch calls handle() from its own callback instead.

private:
  uint32_t windowStartMicros_ = 0;
  uint32_t microsPerTick_ = 0;
  uint32_t quarters_ = 0;
  uint8_t tick_ = 0;
  uint8_t counted_ = 0;
  bool running_ = false;
  bool onQuarter_ = false;
};

} // namespace espmidi

#endif // ESPMIDI_CONTROL_H
