// The smallest useful EspMidi sketch in the other direction: receive MIDI on one
// port and print it.
//
// Connect a keyboard's MIDI OUT and every note appears on the console. **One port,
// one direction** — and the same three steps as every other sketch here.
//
// Wiring: the keyboard's MIDI OUT through a MIDI IN circuit (an optocoupler) to
// RX_PIN. **Do not connect a MIDI socket straight to a GPIO**: MIDI IN has to be
// electrically isolated.

#include <EspMidiUart.h>

static const int8_t RX_PIN = 20;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UartPort din(router, Serial1, 1);
espmidi::AppPort sketch(router, "sketch");

void onMidi(void *, const espmidi::Message &message)
{
  if (message.chunk)
  {
    // A System Exclusive stream, delivered in pieces. There is nothing to say
    // about it in a first sketch; it is carried, not interpreted.
    return;
  }

  switch (message.command())
  {
    case 0x90:
      // A note on with velocity 0 means note off on the wire. Treating it as a
      // note on is the most common beginner's bug in MIDI.
      if (message.data2 == 0)
      {
        Serial.print("note off ");
        break;
      }
      Serial.print("note on  ");
      break;
    case 0x80:
      Serial.print("note off ");
      break;
    case 0xb0:
      Serial.print("control  ");
      break;
    default:
      Serial.print("status ");
      Serial.print(message.status, HEX);
      Serial.print(" ");
      break;
  }

  // channel() is 0-based; MIDI devices label the same channel 1..16.
  if (message.status < 0xf0)
  {
    Serial.print("ch");
    Serial.print(message.channel() + 1);
    Serial.print(" ");
  }
  Serial.print(message.data1);
  Serial.print(" ");
  Serial.println(message.data2);
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  // txPin is -1: this sketch only receives.
  din.begin("MIDI IN", RX_PIN, -1);

  router.addRoute(din.in(), sketch.out());
  sketch.onMessage(onMidi);

  Serial.println("play something");
}

void loop()
{
  din.update();    // read the wire
  router.update(); // run the routing, which calls onMidi()
}
