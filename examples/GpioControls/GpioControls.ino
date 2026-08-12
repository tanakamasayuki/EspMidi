// A MIDI controller: two knobs, two buttons and an encoder, over USB and MIDI DIN.
//
// This is UC10 in docs/USE_CASES.ja.md. The board is a MIDI device rather than a
// MIDI router here, and everything it produces goes out of both a USB MIDI port
// and a MIDI DIN socket at once.
//
// **The helpers never touch a pin.** The sketch reads whatever it has and hands
// the number over, which is why the same helper works for an ADC, a port
// expander, a touch sensor or a value off a network. It is also why the reading
// and the moment are always parameters — see src/EspMidiControl.h.
//
//   knobs, buttons, encoder ──→ [application port] ──→ USB MIDI
//                                                  └─→ MIDI DIN OUT
//   USB MIDI ──→ an LED (note 60) and a dimmer (CC 20)
//
// The LED half is the mirror image: a DAW lights the board's LED by sending a
// note, using the same Filter the routing uses everywhere else.

#include <EspMidiEspUsbDevice.h>
#include <EspMidiUart.h>

static const int8_t DIN_RX_PIN = 20;
static const int8_t DIN_TX_PIN = 19;

static const uint8_t KNOB_PINS[] = {1, 2};
static const uint8_t BUTTON_PINS[] = {4, 5};
static const uint8_t ENCODER_A_PIN = 6;
static const uint8_t ENCODER_B_PIN = 7;
static const uint8_t LED_PIN = 8;

EspUsbDevice usb;
EspUsbDeviceMidi usbMidi(usb, 1, 1);

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UsbDevicePort pc(router, usbMidi, usb);
espmidi::UartPort din(router, Serial1, 1);

// Everything the board itself produces and receives. Control helpers are built on
// an application port, so what they send is routed like any other port's traffic.
espmidi::AppPort controls(router, "controls");

espmidi::Analog knobs[] = {
    espmidi::Analog(controls),
    espmidi::Analog(controls),
};

espmidi::Button buttons[] = {
    espmidi::Button(controls),
    espmidi::Button(controls),
};

espmidi::Encoder encoder(controls);

espmidi::ControlOutput led;
espmidi::ControlOutput dimmer;

void setLed(void *, uint8_t level, const espmidi::Message &)
{
  digitalWrite(LED_PIN, level > 0 ? HIGH : LOW);
}

void setDimmer(void *, uint8_t level, const espmidi::Message &)
{
  // 7 bits onto 8: a dimmer is the same mapping problem as a knob, the other way
  // round.
  analogWrite(LED_PIN, static_cast<int>(level) * 2);
}

// A quadrature encoder read the simple way. Anything that produces a position
// works: a hardware pulse counter, an I2C encoder, a rotary on a port expander.
int32_t readEncoder()
{
  static int32_t position = 0;
  static uint8_t previous = 0;
  const uint8_t state = static_cast<uint8_t>((digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN));
  if (state != previous)
  {
    // Gray code: one bit changes per step, and which one says the direction.
    static const int8_t steps[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    position += steps[(previous << 2) | state];
    previous = state;
  }
  return position;
}

void setup()
{
  Serial.begin(115200);
  for (uint8_t pin : BUTTON_PINS)
  {
    pinMode(pin, INPUT_PULLUP);
  }
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  delay(200);

  // 1) The stacks.
  EspUsbDeviceConfig config;
  config.manufacturer = "EspMidi";
  config.product = "EspMidi Controls";
  usb.begin(config);

  // 2) The ports.
  pc.begin("USB MIDI");
  din.begin("MIDI DIN", DIN_RX_PIN, DIN_TX_PIN);

  // 3) The controls. Each knob and button gets its own controller or note
  //    number; nothing else about them differs.
  knobs[0].config().controller = 7;  // volume
  knobs[1].config().controller = 11; // expression
  buttons[0].config().number = 60;
  buttons[1].config().note = false;
  buttons[1].config().number = 64; // sustain
  buttons[1].config().latch = true;
  encoder.config().mode = espmidi::EncoderMode::RelativeTwosComplement;
  encoder.config().controller = 16;

  // 4) The routes. What the board produces goes to both destinations at once.
  router.addRoute(controls.in(), pc.out(0));
  router.addRoute(controls.in(), din.out());

  // And what arrives drives the LED. A Filter says which messages, exactly as it
  // does on a route.
  espmidi::Filter noteFilter;
  noteFilter.kinds = espmidi::KindNotes;
  noteFilter.noteMin = 60;
  noteFilter.noteMax = 60;
  led = espmidi::ControlOutput(noteFilter, setLed);

  espmidi::Filter dimmerFilter;
  dimmerFilter.kinds = espmidi::KindControlChange;
  dimmerFilter.ccMin = 20;
  dimmerFilter.ccMax = 20;
  dimmer = espmidi::ControlOutput(dimmerFilter, setDimmer);

  controls.onMessage([](void *, const espmidi::Message &message) {
    led.handle(message);
    dimmer.handle(message);
  });
  router.addRoute(pc.in(0), controls.out());

  Serial.println("EspMidi controls ready");
}

void loop()
{
  const uint32_t nowMs = millis();

  // The helpers are told what was read and when. Nothing here is inside them.
  for (size_t i = 0; i < sizeof(KNOB_PINS) / sizeof(KNOB_PINS[0]); i++)
  {
    knobs[i].update(static_cast<uint16_t>(analogRead(KNOB_PINS[i])));
  }
  for (size_t i = 0; i < sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]); i++)
  {
    // INPUT_PULLUP, so a pressed button reads LOW.
    buttons[i].update(digitalRead(BUTTON_PINS[i]) == LOW, nowMs);
  }
  encoder.update(readEncoder());

  usb.task();
  pc.update();
  din.update();
  router.update();
}
