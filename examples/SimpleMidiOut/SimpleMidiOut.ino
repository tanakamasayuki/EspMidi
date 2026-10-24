// en: Send MIDI messages to an output port
// ja: MIDIメッセージを出力ポートへ送る
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
  // en: Initialize this component after its prerequisites; inspect the returned status where provided.
  // ja: 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
  din.begin("MIDI OUT", -1, TX_PIN);

  // en: Register source and destination endpoints/groups, considering disconnects and unintended loops.
  // ja: 切断と意図しないループを考慮して、入力と出力のEndpoint／グループを登録します。
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
