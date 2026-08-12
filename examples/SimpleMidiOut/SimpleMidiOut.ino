// The smallest useful EspMidi sketch: send MIDI out of one port.
//
// It plays a C major scale over and over, so a sound module connected to the MIDI
// OUT makes a noise as soon as the board is powered. **One port, one direction** —
// which is the simplest thing this library does, and the shape everything else is
// built from.
//
// Wiring: TX_PIN through a MIDI OUT circuit (two 220Ω resistors) to the sound
// module's MIDI IN. Nothing else is needed; the MIDI IN side is not used here.

#include <EspMidiUart.h>

static const int8_t TX_PIN = 19;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

// The MIDI socket.
espmidi::UartPort din(router, Serial1, 1);

// The sketch itself, as a port. Anything it sends is routed like any other port's
// traffic — which is why adding a second destination later is one more route
// rather than a rewrite.
espmidi::AppPort sketch(router, "sketch");

void setup()
{
  // rxPin is -1: this sketch only sends, so the port has no input pin.
  din.begin("MIDI OUT", -1, TX_PIN);

  router.addRoute(sketch.in(), din.out());
}

void playNote(uint8_t note, uint16_t milliseconds)
{
  // 0x90 is "note on, channel 1". Sending only queues it.
  sketch.sendShort(0x90, note, 100);
  router.update(); // this is what actually puts it on the wire
  delay(milliseconds);

  sketch.sendShort(0x80, note, 0); // note off
  router.update();
}

void loop()
{
  static const uint8_t scale[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (uint8_t note : scale)
  {
    playNote(note, 250);
    delay(50);
  }
  delay(1000);
}
