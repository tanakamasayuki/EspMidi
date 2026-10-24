// en: Observe MIDI arriving on UART
// ja: UARTへ届くMIDIを観察する
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
  // en: Initialize this component after its prerequisites; inspect the returned status where provided.
  // ja: 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
  linkPort.begin("MIDI link", RX_PIN, TX_PIN);
  thru.begin("MIDI thru", THRU_RX_PIN, THRU_TX_PIN);

  // 3) The routes.
  // en: Register source and destination endpoints/groups, considering disconnects and unintended loops.
  // ja: 切断と意図しないループを考慮して、入力と出力のEndpoint／グループを登録します。
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
