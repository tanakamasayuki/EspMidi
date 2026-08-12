// DUT side of the USB Device MIDI port test: a plain EspUsbHost watching the
// board that runs EspMidi's USB Device port.
//
// This side deliberately does not use EspMidi. The USB Host port is Phase 7; what
// is under test here is the device half, so the observer is the raw library. That
// also keeps one bug from hiding another: if both ends built their packets with
// the same code, a wrong cable nibble would cancel out.
//
// The one check that cannot be replaced by a round trip is getMidiPortInfo(). A
// received message's cable number is read straight out of its packet header, so a
// device that declared a single cable would still echo cable 2 back correctly.
// Reading the counts EspUsbHost decoded from the descriptor is the only way to
// show that the device really advertised its ports.

#include "EspUsbHost.h"

EspUsbHost usb;

// Host-view: IN is device to host, OUT is host to device. This side sends on the
// OUT cables and receives on the IN ones. The peer declares 2 IN and 3 OUT.

bool g_greeted = false;
uint32_t g_nextBanner = 0;

static bool sendOnCable(uint8_t cable, uint8_t status, uint8_t data1, uint8_t data2)
{
  // midiSendNoteOn() and friends have no cable argument, so the packet is built by
  // hand: the cable is the high nibble of the header and the low nibble is the
  // Code Index Number, which for a channel voice message is the status's own high
  // nibble.
  const uint8_t packet[] = {
      static_cast<uint8_t>((cable << 4) | (status >> 4)),
      status,
      data1,
      data2,
  };
  return usb.midiSend(packet, sizeof(packet));
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    {
                      Serial.printf("MIDI_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                                    message.cable,
                                    message.codeIndex,
                                    message.status,
                                    message.data1,
                                    message.data2);
                    });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (!g_greeted && static_cast<int32_t>(millis() - g_nextBanner) >= 0)
  {
    Serial.println("HOST_READY");
    g_nextBanner = millis() + 500;
  }

  if (Serial.available() > 0)
  {
    g_greeted = true;
    const char command = Serial.read();
    if (command == 'i')
    {
      // Enumeration may still be in progress when the test asks, so the answer is
      // waited for rather than sampled once.
      EspUsbHostMidiPortInfo info;
      bool ok = false;
      const uint32_t until = millis() + 5000;
      while (static_cast<int32_t>(until - millis()) > 0)
      {
        ok = usb.getMidiPortInfo(info);
        if (ok)
        {
          break;
        }
        // EspUsbHost runs its own task, so there is nothing to pump here.
        delay(10);
      }
      Serial.printf("MIDI_PORT_INFO ok=%u in=%u out=%u iface=%u\n",
                    ok ? 1 : 0,
                    info.inCableCount,
                    info.outCableCount,
                    info.interfaceNumber);
    }
    else if (command == 'n')
    {
      // Cable 2 exists only in the host-to-device direction: the peer declared 3
      // that way and 2 the other. A port that mixed the two counts up would have
      // no seat for this to land on.
      Serial.printf("MIDI_TX_CABLE2 %u\n", sendOnCable(2, 0x90, 0x3c, 0x64) ? 1 : 0);
    }
    else if (command == 'c')
    {
      Serial.printf("MIDI_TX_CABLE0 %u\n", sendOnCable(0, 0xb0, 0x07, 0x40) ? 1 : 0);
    }
    else if (command == 'x')
    {
      // A cable the peer never declared. It has to be dropped rather than landing
      // on whichever seat happens to be there.
      Serial.printf("MIDI_TX_CABLE7 %u\n", sendOnCable(7, 0x90, 0x30, 0x40) ? 1 : 0);
    }
    else if (command == 'S')
    {
      // CIN 0x4 = SysEx starts / continues, 0x6 = ends with two bytes.
      const uint8_t packets[] = {
          0x24, 0xf0, 0x7d, 0x11,
          0x26, 0x12, 0xf7, 0x00,
      };
      Serial.printf("MIDI_TX_SYSEX %u\n", usb.midiSend(packets, sizeof(packets)) ? 1 : 0);
    }
  }

  delay(1);
}
