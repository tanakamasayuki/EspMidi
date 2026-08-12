// UART MIDI monitor.
//
// Watches a MIDI 1.0 stream on a UART and prints what arrives to the console,
// while passing it through to a second UART unchanged. Put it between a
// keyboard and a sound module and you can see what the keyboard actually sends
// without taking the sound module out of the chain.
//
// Wiring: the MIDI IN of the monitored link goes to RX_PIN, and TX_PIN drives
// the MIDI OUT that carries it onward. A real MIDI DIN socket needs an
// optocoupler on the input and a 220Ω pair on the output; that is outside this
// library, which deals in bytes.
//
// The console is UART0, the USB-serial chip on the board — not the MIDI UARTs.

#include <EspMidiUart.h>

// The MIDI link being watched.
static const int8_t RX_PIN = 20;
static const int8_t TX_PIN = 19;

// Where it is passed on to. Set both to -1 to monitor without passing through.
static const int8_t THRU_RX_PIN = 17;
static const int8_t THRU_TX_PIN = 18;

espmidi::PortRegistry registry;
espmidi::Router router(registry);

espmidi::UartPort linkPort(router, Serial1, 1);
espmidi::UartPort thru(router, Serial2, 2);

// A port with no transport behind it. Routing delivers to it exactly as it does
// to a UART, which is how the sketch gets to look at a message without sitting
// in the path of the one being passed through.
espmidi::AppPort monitor(router, "monitor");

void printMessage(void *, const espmidi::Message &message)
{
  if (message.chunk)
  {
    // A dump crosses the library without being copied, so this is a pointer into
    // the buffer it arrived in. It is valid for this call only.
    if (message.chunkStart)
    {
      Serial.print("SysEx");
    }
    Serial.print(" ");
    Serial.print(message.chunkLength);
    Serial.print(" bytes");
    if (message.chunkEnd)
    {
      Serial.println(" (end)");
    }
    return;
  }

  switch (message.command())
  {
    case 0x90:
      Serial.print("Note On  ");
      break;
    case 0x80:
      Serial.print("Note Off ");
      break;
    case 0xb0:
      Serial.print("CC       ");
      break;
    case 0xc0:
      Serial.print("Program  ");
      break;
    case 0xe0:
      Serial.print("Bend     ");
      break;
    default:
      Serial.print("status ");
      Serial.print(message.status, HEX);
      Serial.print(" ");
      break;
  }

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

  // 1) The stacks. A UART port opens its own serial port, so there is nothing
  //    else to start here.

  // 2) The ports.
  linkPort.begin("MIDI link", RX_PIN, TX_PIN);
  thru.begin("MIDI thru", THRU_RX_PIN, THRU_TX_PIN);

  // 3) The routes.
  router.addRoute(linkPort.in(), thru.out());
  router.addRoute(linkPort.in(), monitor.out());
  monitor.onMessage(printMessage);

  // The clock is 24 messages per beat and would fill the console on its own.
  // Everything else goes to the monitor; the pass-through route is untouched,
  // so what reaches the sound module is still the complete stream.
  espmidi::Filter quiet;
  quiet.kinds = static_cast<uint16_t>(espmidi::KindAll & ~espmidi::KindSystemRealTime);
  router.setOutPortFilter(monitor.out(), quiet);

  Serial.println("EspMidi UART monitor ready");
}

void loop()
{
  linkPort.update();
  thru.update();
  router.update();
}
