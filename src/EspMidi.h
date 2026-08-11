// EspMidi - MIDI port integration and routing for ESP32.
//
// This is the public header. A sketch includes it for the core: the common MIDI
// message representation, the port model, routing, filtering and transformation.
// It pulls in no Arduino, ESP-IDF or hardware dependency, so the same code is
// compiled and tested on the host (see tests/unit/).
//
// Ports live in their own headers and are the only place that touches a
// transport library. Include just the ones a sketch uses:
//
//   #include <EspMidiUart.h>          // UART MIDI (bundled, needs HardwareSerial)
//   #include <EspMidiEspUsbHost.h>    // needs EspUsbHost
//   #include <EspMidiEspUsbDevice.h>  // needs EspUsbDevice
//   #include <EspMidiEspBle.h>        // needs EspBle
//
// EspMidi never starts, stops or owns a USB or BLE stack: the sketch owns it and
// a port only subscribes to what it is handed. That is what lets MIDI coexist
// with HID, CDC, Audio and custom GATT services in the same sketch, and it is
// why the transport libraries keep their own MIDI convenience APIs — EspMidi is
// worth using when several interfaces meet, not when one is used on its own.
//
// Design documents (Japanese):
//   docs/REQUIREMENTS.ja.md  what this library is for and what it excludes
//   docs/DATA_MODEL.ja.md    the message representation and the port model
//   docs/ROUTING.ja.md       routes, the pipeline, driving, SysEx rules
//   docs/CORE_DESIGN.ja.md   core / port / example boundaries and concurrency
//   docs/DECISIONS.ja.md     why it is shaped this way, and what was rejected

#ifndef ESPMIDI_H
#define ESPMIDI_H

#include "espmidi_version.h"

// The library is being built up in the order given by
// docs/DEVELOPMENT_PLAN.ja.md. Nothing is declared here yet: this header
// currently exists so the include path, the Arduino library layout and the host
// build are all verified from the first commit (tests/unit/arduino_smoke/).
namespace espmidi
{
}

#endif // ESPMIDI_H
