// Control mapping: buttons, knobs, encoders, driving something from received
// MIDI, and clock.
//
// The helpers take the reading and the moment as parameters, which is what makes
// this test possible: a bouncing switch, a wandering ADC and a tempo change are
// all just numbers here, with time moved by hand. On hardware none of that can be
// arranged reliably.
//
// Specification: docs/REQUIREMENTS.ja.md.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <vector>

namespace
{
int g_ran = 0;

struct Sent
{
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
};

struct Sink
{
  std::vector<Sent> messages;

  static bool write(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    self->messages.push_back(Sent{message.status, message.data1, message.data2});
    return true;
  }
};

// An application port with somewhere for what it produces to go.
struct Fixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  espmidi::AppPort app{router, "controls"};
  Sink sink;

  Fixture()
  {
    espmidi::EndpointIdentity identity;
    identity.transport = espmidi::Transport::Uart;
    const espmidi::EndpointId endpoint = registry.attachEndpoint(identity, "out");
    const espmidi::OutPort out = registry.attachOutPort(endpoint, 0);
    router.setOutputSink(out, &Sink::write, &sink);
    router.addRoute(app.in(), out);
  }

  void flush() { router.update(); }

  size_t count()
  {
    flush();
    return sink.messages.size();
  }

  const Sent &last()
  {
    flush();
    assert(!sink.messages.empty());
    return sink.messages.back();
  }
};

// --- Button ---------------------------------------------------------------

void test_button_sends_a_note_on_and_off()
{
  Fixture f;
  espmidi::Button button(f.app);

  // The first reading is the resting state and announces nothing: a board that
  // starts with a pedal held down must not report a note nobody played.
  assert(!button.update(false, 0));
  assert(f.count() == 0);

  assert(button.update(true, 100));
  assert(f.last().status == 0x90);
  assert(f.last().data1 == 60);
  assert(f.last().data2 == 127);
  assert(button.on());

  assert(button.update(false, 200));
  assert(f.last().status == 0x80);
  assert(f.last().data2 == 0);
  assert(!button.on());
}

void test_button_debounces()
{
  // A real switch bounces for a few milliseconds. Without this, one press is a
  // burst of notes.
  Fixture f;
  espmidi::Button button(f.app);
  button.config().debounceMs = 20;

  button.update(false, 0);
  assert(button.update(true, 100));
  assert(f.count() == 1);

  // Bouncing: ignored, and the state stays as it was.
  assert(!button.update(false, 105));
  assert(!button.update(true, 110));
  assert(f.count() == 1);
  assert(button.on());

  // Still enough later, the release is believed.
  assert(button.update(false, 130));
  assert(f.count() == 2);
  assert(f.last().status == 0x80);
}

void test_button_latches()
{
  // A momentary switch as a toggle, which is what a switch selecting a mode wants.
  Fixture f;
  espmidi::Button button(f.app);
  button.config().latch = true;

  button.update(false, 0);
  assert(button.update(true, 100)); // press: on
  assert(button.on());
  assert(!button.update(false, 200)); // release: nothing
  assert(f.count() == 1);

  assert(button.update(true, 300)); // press again: off
  assert(!button.on());
  assert(f.last().status == 0x80);
  assert(f.count() == 2);
}

void test_button_can_send_a_control_change()
{
  Fixture f;
  espmidi::ButtonConfig config;
  config.note = false;
  config.number = 64; // sustain
  config.channel = 3;
  espmidi::Button button(f.app, config);

  button.update(false, 0);
  button.update(true, 100);

  assert(f.last().status == 0xb3);
  assert(f.last().data1 == 64);
  assert(f.last().data2 == 127);
}

// --- Analog ---------------------------------------------------------------

void test_analog_maps_the_range()
{
  Fixture f;
  espmidi::Analog knob(f.app);
  knob.config().hysteresis = 0;

  knob.update(0); // the first reading only establishes where it is
  assert(f.count() == 0);
  assert(knob.value() == 0);

  knob.update(4095);
  assert(f.last().status == 0xb0);
  assert(f.last().data1 == 7);
  assert(f.last().data2 == 127);

  knob.update(2048);
  assert(f.last().data2 == 64);
}

void test_analog_ignores_a_wandering_reading()
{
  // An ADC on a long wire moves by a few counts on its own. Without hysteresis a
  // knob nobody is touching fills the link with control changes.
  Fixture f;
  espmidi::Analog knob(f.app);
  knob.config().hysteresis = 16;

  knob.update(2000);
  assert(!knob.update(2008));
  assert(!knob.update(1993));
  assert(f.count() == 0);

  // A real move is believed.
  assert(knob.update(2100));
  assert(f.count() == 1);
}

void test_analog_sends_only_on_a_change_of_value()
{
  // 4096 raw steps onto 128 values: most movements land on the same value, and
  // resending it would be noise.
  Fixture f;
  espmidi::Analog knob(f.app);
  knob.config().hysteresis = 1;

  knob.update(2048);
  const uint8_t value = knob.value();
  assert(!knob.update(2049)); // still the same 7-bit value
  assert(knob.value() == value);
  assert(f.count() == 0);
}

void test_analog_can_be_reversed_and_limited()
{
  // A pedal wired backwards, and an output that never reaches full: both are
  // configuration rather than rewiring.
  Fixture f;
  espmidi::AnalogConfig config;
  config.rawMin = 4095;
  config.rawMax = 0;
  config.outLow = 0;
  config.outHigh = 100;
  config.hysteresis = 0;
  espmidi::Analog pedal(f.app, config);

  pedal.update(4095);
  assert(pedal.value() == 0);
  pedal.update(0);
  assert(pedal.value() == 100);
}

void test_analog_clamps_a_reading_outside_its_range()
{
  Fixture f;
  espmidi::AnalogConfig config;
  config.rawMin = 100;
  config.rawMax = 3000;
  config.hysteresis = 0;
  espmidi::Analog knob(f.app, config);

  knob.update(0);
  assert(knob.value() == 0);
  knob.update(4095);
  assert(knob.value() == 127);
}

void test_analog_smoothing_settles()
{
  // Smoothing must arrive, not stop one count short forever.
  Fixture f;
  espmidi::AnalogConfig config;
  config.smoothing = 2;
  config.hysteresis = 0;
  espmidi::Analog knob(f.app, config);

  knob.update(0);
  for (int i = 0; i < 200; i++)
  {
    knob.update(4000);
  }
  assert(knob.raw() == 4000);
  assert(knob.value() == 124);
}

// --- Encoder --------------------------------------------------------------

void test_encoder_absolute()
{
  Fixture f;
  espmidi::Encoder encoder(f.app);

  encoder.update(0); // the first position only establishes where it is
  assert(f.count() == 0);

  assert(encoder.update(3));
  assert(f.last().status == 0xb0);
  assert(f.last().data1 == 16);
  assert(f.last().data2 == 67);

  assert(encoder.update(1));
  assert(f.last().data2 == 65);
}

void test_encoder_absolute_stops_at_the_ends()
{
  Fixture f;
  espmidi::Encoder encoder(f.app);
  encoder.update(0);

  encoder.update(1000);
  assert(encoder.value() == 127);
  const size_t sent = f.count();

  // Already at the top: nothing more to say.
  assert(!encoder.update(2000));
  assert(f.count() == sent);
}

void test_encoder_relative_conventions()
{
  // There is no standard for this, and the wrong one turns the parameter the
  // wrong way or not at all.
  Fixture f;
  espmidi::EncoderConfig config;
  config.mode = espmidi::EncoderMode::RelativeTwosComplement;
  espmidi::Encoder encoder(f.app, config);
  encoder.update(0);

  encoder.update(1);
  assert(f.last().data2 == 1);
  encoder.update(0);
  assert(f.last().data2 == 127); // -1

  encoder.config().mode = espmidi::EncoderMode::RelativeSignedBit;
  encoder.update(1);
  assert(f.last().data2 == 65); // +1
  encoder.update(0);
  assert(f.last().data2 == 1); // -1

  encoder.config().mode = espmidi::EncoderMode::RelativeBinaryOffset;
  encoder.update(1);
  assert(f.last().data2 == 65);
  encoder.update(0);
  assert(f.last().data2 == 63);
}

void test_encoder_a_fast_turn_is_not_lost()
{
  // A relative message carries at most 63. Clamping would throw away part of a
  // fast turn; splitting keeps all of it.
  Fixture f;
  espmidi::EncoderConfig config;
  config.mode = espmidi::EncoderMode::RelativeTwosComplement;
  espmidi::Encoder encoder(f.app, config);
  encoder.update(0);

  encoder.update(100);
  f.flush();

  assert(f.sink.messages.size() == 2);
  assert(f.sink.messages[0].data2 == 63);
  assert(f.sink.messages[1].data2 == 37);
}

void test_encoder_step()
{
  Fixture f;
  espmidi::EncoderConfig config;
  config.step = 5;
  espmidi::Encoder encoder(f.app, config);
  encoder.update(0);

  encoder.update(2);
  assert(encoder.value() == 64 + 10);
}

// --- Driving something from received MIDI ---------------------------------

struct Lamp
{
  std::vector<uint8_t> levels;

  static void set(void *context, uint8_t level, const espmidi::Message &)
  {
    static_cast<Lamp *>(context)->levels.push_back(level);
  }
};

espmidi::Message shortMessage(uint8_t *storage, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
{
  espmidi::Message message;
  espmidi::buildShortMessage(message, storage, status, d1, d2);
  return message;
}

void test_control_output_follows_notes()
{
  Lamp lamp;
  espmidi::Filter filter;
  filter.kinds = espmidi::KindNotes;
  filter.noteMin = 60;
  filter.noteMax = 60;
  espmidi::ControlOutput lampOutput(filter, &Lamp::set, &lamp);

  uint8_t storage[espmidi::MaxShortMessageBytes] = {};
  assert(lampOutput.handle(shortMessage(storage, 0x90, 60, 100)));
  assert(lamp.levels.size() == 1);
  assert(lamp.levels[0] == 100);

  assert(lampOutput.handle(shortMessage(storage, 0x80, 60, 0)));
  assert(lamp.levels[1] == 0);

  // A note on with velocity 0 is a note off on the wire, so the lamp goes out
  // rather than staying lit forever.
  assert(lampOutput.handle(shortMessage(storage, 0x90, 60, 0)));
  assert(lamp.levels[2] == 0);

  // Another note, and another kind of message: not this lamp's business.
  assert(!lampOutput.handle(shortMessage(storage, 0x90, 62, 100)));
  assert(!lampOutput.handle(shortMessage(storage, 0xb0, 7, 64)));
  assert(lamp.levels.size() == 3);
}

void test_control_output_follows_a_controller()
{
  Lamp lamp;
  espmidi::Filter filter;
  filter.kinds = espmidi::KindControlChange;
  filter.ccMin = 20;
  filter.ccMax = 20;
  espmidi::ControlOutput dimmer(filter, &Lamp::set, &lamp);

  uint8_t storage[espmidi::MaxShortMessageBytes] = {};
  assert(dimmer.handle(shortMessage(storage, 0xb0, 20, 90)));
  assert(dimmer.level() == 90);
  assert(!dimmer.handle(shortMessage(storage, 0xb0, 21, 90)));
}

void test_control_output_ignores_data_streams()
{
  Lamp lamp;
  espmidi::ControlOutput output(espmidi::Filter(), &Lamp::set, &lamp);

  espmidi::Message chunk;
  chunk.chunk = true;
  chunk.status = 0xf0;
  assert(!output.handle(chunk));
  assert(lamp.levels.empty());
}

void test_control_output_attaches_to_an_application_port()
{
  // The shape a sketch actually uses: routing delivers to the port, the port
  // hands the message to the binding.
  Fixture f;
  Lamp lamp;
  espmidi::Filter filter;
  filter.kinds = espmidi::KindNotes;
  espmidi::ControlOutput lampOutput(filter, &Lamp::set, &lamp);

  espmidi::AppPort watcher{f.router, "lamp", 1};
  watcher.onMessage(&espmidi::ControlOutput::receive, &lampOutput);
  f.router.addRoute(f.app.in(), watcher.out());

  f.app.sendShort(0x90, 60, 77);
  f.router.update();

  assert(lamp.levels.size() == 1);
  assert(lamp.levels[0] == 77);
}

// --- Clock ----------------------------------------------------------------

void test_clock_tempo_arithmetic()
{
  // 120 BPM is 500 ms a quarter, 24 ticks a quarter, so 20833 us a tick.
  assert(espmidi::microsPerClockTick(12000) == 20833);
  assert(espmidi::bpmTimes100FromTick(20833) == 12000);
  assert(espmidi::microsPerClockTick(6000) == 41666); // 60 BPM
  assert(espmidi::microsPerClockTick(0) == 0);
  assert(espmidi::bpmTimes100FromTick(0) == 0);
}

void test_clock_generator_sends_twenty_four_ticks_a_quarter()
{
  Fixture f;
  espmidi::ClockGenerator clock(f.app);
  clock.setTempo(12000); // 120 BPM

  assert(clock.start(0));
  f.flush();
  assert(f.sink.messages[0].status == 0xfa);

  // Half a second is one quarter note. The tick at time 0 goes out with start, so
  // a full quarter is 24 of them and the 25th is due at the next quarter.
  size_t ticks = 0;
  for (uint32_t now = 0; now < 500000; now += 1000)
  {
    ticks += clock.update(now);
  }
  assert(ticks == 24);

  f.flush();
  size_t clocks = 0;
  for (const Sent &message : f.sink.messages)
  {
    clocks += message.status == 0xf8 ? 1 : 0;
  }
  assert(clocks == 24);
}

void test_clock_generator_transport()
{
  Fixture f;
  espmidi::ClockGenerator clock(f.app);
  clock.setTempo(12000);

  assert(!clock.running());
  assert(clock.update(1000000) == 0); // stopped: nothing is due

  clock.start(0);
  assert(clock.running());
  clock.stop();
  assert(!clock.running());
  assert(clock.update(1000000) == 0);

  clock.resume(2000000);
  assert(clock.running());
  f.flush();
  assert(f.sink.messages[0].status == 0xfa);
  assert(f.sink.messages.back().status == 0xfb);
}

void test_clock_generator_resynchronises_after_a_stall()
{
  // A long SysEx or a blocking write can hold loop() for a while. Catching up
  // completely would send a burst of clocks that means nothing musically.
  Fixture f;
  espmidi::ClockGenerator clock(f.app);
  clock.setTempo(12000);
  clock.start(0);

  const size_t sent = clock.update(10000000); // ten seconds late
  assert(sent == espmidi::ClockGenerator::MaxCatchUpTicks);

  // And it is back on schedule rather than still owing hundreds of ticks.
  assert(clock.update(10000000) == 0);
  assert(clock.update(10021000) == 1);
}

void test_clock_counter_finds_the_beat_and_the_tempo()
{
  espmidi::ClockCounter counter;
  uint8_t storage[espmidi::MaxShortMessageBytes] = {};

  assert(counter.handle(shortMessage(storage, 0xfa), 0));
  assert(counter.running());

  // 120 BPM: a tick every 20833 us.
  uint32_t now = 0;
  for (int i = 0; i < 24; i++)
  {
    now += 20833;
    assert(counter.handle(shortMessage(storage, 0xf8), now));
  }

  assert(counter.quarters() == 1);
  assert(counter.onQuarter());
  assert(counter.tick() == 0);
  // Measured over a quarter note, so the reading is the tempo it was given.
  assert(counter.bpmTimes100() >= 11950 && counter.bpmTimes100() <= 12050);

  now += 20833;
  counter.handle(shortMessage(storage, 0xf8), now);
  assert(!counter.onQuarter());
  assert(counter.tick() == 1);
}

void test_clock_counter_transport_and_other_messages()
{
  espmidi::ClockCounter counter;
  uint8_t storage[espmidi::MaxShortMessageBytes] = {};

  // A note is not this counter's business.
  assert(!counter.handle(shortMessage(storage, 0x90, 60, 100), 0));

  // Some senders never send Start; the first tick is enough to begin counting.
  assert(counter.handle(shortMessage(storage, 0xf8), 1000));
  assert(counter.running());

  assert(counter.handle(shortMessage(storage, 0xfc), 2000));
  assert(!counter.running());

  counter.reset();
  assert(counter.quarters() == 0);
  assert(counter.microsPerTick() == 0);
}

void test_clock_converts_from_one_side_to_the_other()
{
  // The two halves composed: measure an incoming clock, hand the tempo to a
  // generator, and the board is a clock converter.
  Fixture f;
  espmidi::ClockCounter counter;
  espmidi::ClockGenerator clock(f.app);
  uint8_t storage[espmidi::MaxShortMessageBytes] = {};

  uint32_t now = 0;
  counter.handle(shortMessage(storage, 0xfa), now);
  for (int i = 0; i < 24; i++)
  {
    now += 41666; // 60 BPM
    counter.handle(shortMessage(storage, 0xf8), now);
  }

  clock.setMicrosPerTick(counter.microsPerTick());
  assert(clock.bpmTimes100() >= 5950 && clock.bpmTimes100() <= 6050);

  clock.start(now);
  size_t ticks = 0;
  const uint32_t until = now + 1000000; // a second, which at 60 BPM is a quarter
  for (uint32_t t = now; t < until; t += 1000)
  {
    ticks += clock.update(t);
  }
  assert(ticks == 24);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_button_sends_a_note_on_and_off);
  run(test_button_debounces);
  run(test_button_latches);
  run(test_button_can_send_a_control_change);

  run(test_analog_maps_the_range);
  run(test_analog_ignores_a_wandering_reading);
  run(test_analog_sends_only_on_a_change_of_value);
  run(test_analog_can_be_reversed_and_limited);
  run(test_analog_clamps_a_reading_outside_its_range);
  run(test_analog_smoothing_settles);

  run(test_encoder_absolute);
  run(test_encoder_absolute_stops_at_the_ends);
  run(test_encoder_relative_conventions);
  run(test_encoder_a_fast_turn_is_not_lost);
  run(test_encoder_step);

  run(test_control_output_follows_notes);
  run(test_control_output_follows_a_controller);
  run(test_control_output_ignores_data_streams);
  run(test_control_output_attaches_to_an_application_port);

  run(test_clock_tempo_arithmetic);
  run(test_clock_generator_sends_twenty_four_ticks_a_quarter);
  run(test_clock_generator_transport);
  run(test_clock_generator_resynchronises_after_a_stall);
  run(test_clock_counter_finds_the_beat_and_the_tempo);
  run(test_clock_counter_transport_and_other_messages);
  run(test_clock_converts_from_one_side_to_the_other);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
