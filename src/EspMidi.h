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

// The common representation every port and every routing rule is expressed in.
#include "EspMidiMessage.h"

// Endpoints, ports, seats and port groups: what routing addresses.
#include "EspMidiPort.h"

// Declarative filtering and transformation, the rules a stage carries.
#include "EspMidiFilter.h"

// Routes, the pipeline that carries messages between ports, and the application
// port a sketch uses to watch or inject.
#include "EspMidiRouter.h"

// Wire-format codecs. They live in the core rather than in the ports because
// the same code serves more than one transport and because keeping them here is
// what lets every wire format be tested on the host (tests/unit/).
#include "EspMidiParser.h"    // MIDI 1.0 byte stream (UART)
#include "EspMidiUsbPacket.h" // USB MIDI 1.0 event packets (USB Host and Device)

// Control mapping: buttons, knobs, encoders and clock, as MIDI. Core rather than
// a port, because none of it touches a pin or reads the time itself — the sketch
// hands over the reading and the moment, which is what makes it portable and what
// keeps it working for an input this library has never heard of.
#include "EspMidiControl.h"

#endif // ESPMIDI_H
