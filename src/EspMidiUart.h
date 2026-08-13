// UART MIDI port.
//
// Specification: docs/PORTS.ja.md.
//
// A serial port running at 31250 baud carrying a MIDI 1.0 byte stream. The
// protocol and the peripheral grew up together — 31250 baud exists because it
// divides cleanly from the clock of a 1980s microcontroller — so this port is
// bundled here rather than living in a transport library of its own.
//
// It is the thinnest of the ports: the wire format is the same MIDI 1.0 stream
// the core already parses and serializes, so this file is only the plumbing
// between a HardwareSerial and the router.
//
//   Serial RX --> Parser --> router.receive()  (queued, run from update())
//   Serial TX <-- Serializer <-- router's output sink
//
// The physical layer is outside this port. What the TX and RX pins connect to —
// a 5V current loop through an optocoupler and a 220Ω resistor for a real MIDI
// DIN socket, or another board's UART directly — is the sketch's business and
// the hardware's. This port deals in bytes.
//
// Include this header only in a sketch that wants a UART port; EspMidi.h does
// not pull it in, so a sketch that has no use for Arduino's HardwareSerial does
// not pay for it.

#ifndef ESPMIDI_UART_H
#define ESPMIDI_UART_H

#include "EspMidi.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// Bytes read from the serial port in one update(). A chunk of a data stream is
// handed onward without being copied, so this is also the largest SysEx chunk
// this port produces. It is not a limit on the size of a dump.
#ifndef ESPMIDI_UART_RX_BYTES
#define ESPMIDI_UART_RX_BYTES 64
#endif

// The line configuration passed to HardwareSerial::begin(). MIDI 1.0 is 8 data
// bits, no parity, one stop bit.
#ifndef ESPMIDI_UART_CONFIG
#if defined(ARDUINO)
#define ESPMIDI_UART_CONFIG SERIAL_8N1
#else
#define ESPMIDI_UART_CONFIG 0x800001cu
#endif
#endif

namespace espmidi
{

// The one baud rate MIDI 1.0 has.
static constexpr unsigned long UartMidiBaud = 31250;

// The port itself, over any serial object that answers the HardwareSerial calls
// used below. The template exists so the port's own logic — parsing into the
// router, framing on the way out, what happens when the transport refuses a
// byte — is fixed by tests on the host, leaving the loopback test on hardware to
// prove only what it alone can prove: that the bytes reach the wire.
template <typename SerialType>
class BasicUartPort
{
public:
  // `index` distinguishes this port from other UART ports in the same sketch and
  // is what a seat is matched on, so it must stay the same across a begin() and
  // end() cycle. Passing the UART peripheral number is the obvious choice.
  BasicUartPort(Router &router, SerialType &serial, uint8_t index = 0)
      : router_(router), serial_(serial), index_(index)
  {
  }

  // Opens the serial port and supplies one endpoint with one input and one
  // output. Idempotent, like the registry calls underneath it: calling begin()
  // again after a reconfiguration returns the same seats.
  //
  // `rxPin` and `txPin` are passed through to HardwareSerial::begin(); -1 leaves
  // the peripheral's default pins.
  //
  // **Moving to different pins closes the port first.** On an ESP32 a peripheral
  // reaches a pad through the GPIO matrix, and opening a second pad does not take
  // the first one back: without this, calling begin() again with new pins leaves
  // the old ones attached and the board transmits on a pin nobody asked for. It
  // cost an afternoon on a bench to find, so it is fixed here rather than written
  // down as a caveat.
  //
  // Calling begin() with the same pins stays what it was — a reconfiguration that
  // keeps the seats and whatever has already arrived. Closing and reopening for
  // that would drop the receive buffer and put a glitch on the line.
  bool begin(const char *name = "UART MIDI", int8_t rxPin = -1, int8_t txPin = -1)
  {
    // -1 means "leave that pin as it is", so it never counts as a move.
    const bool moved = (rxPin >= 0 && rxPin != rxPin_) || (txPin >= 0 && txPin != txPin_);
    if (started_ && moved)
    {
      // The same order end() uses: a dump that is being sent is terminated so the
      // device on the other end can discard it (docs/ROUTING.ja.md, rule 2).
      serializer_.closeStream([this](const uint8_t *bytes, size_t length) { return writeBytes(bytes, length); });
      serial_.end();
      started_ = false;
    }

    EndpointIdentity identity;
    identity.transport = Transport::Uart;
    identity.index = index_;

    endpoint_ = router_.registry().attachEndpoint(identity, name);
    if (!endpoint_.valid())
    {
      return false;
    }

    in_ = router_.registry().attachInPort(endpoint_, 0);
    out_ = router_.registry().attachOutPort(endpoint_, 0);
    if (!in_.valid() || !out_.valid())
    {
      return false;
    }

    parser_.setPort(in_.port);
    parser_.reset();
    serializer_.reset();

    serial_.begin(UartMidiBaud, ESPMIDI_UART_CONFIG, rxPin, txPin);
    started_ = true;
    if (rxPin >= 0)
    {
      rxPin_ = rxPin;
    }
    if (txPin >= 0)
    {
      txPin_ = txPin;
    }

    return router_.setOutputSink(out_, &BasicUartPort::sendFrom, this);
  }

  // Closes the serial port and marks the endpoint disconnected. A data stream
  // that was being sent is terminated first: the device on the other end is
  // holding a partial dump, and an 0xF7 is what lets it discard it and carry on
  // (docs/ROUTING.ja.md, rule 2).
  void end()
  {
    if (started_)
    {
      serializer_.closeStream([this](const uint8_t *bytes, size_t length) { return writeBytes(bytes, length); });
      serial_.end();
      started_ = false;
    }

    parser_.reset();
    serializer_.reset();

    if (endpoint_.valid())
    {
      router_.registry().detachEndpoint(endpoint_);
    }
  }

  // Reads whatever has arrived and hands it to the router. Call it from loop(),
  // alongside Router::update(). Nothing is sent from here — the router sends
  // when it drains its queue — so the order of the two calls only decides
  // whether a byte read now is delivered now or on the next pass.
  void update()
  {
    if (!started_)
    {
      return;
    }

    // Bounded per call: a device streaming a large dump must not be able to hold
    // loop() indefinitely, and the bytes it does not get to now are still in the
    // driver's buffer for the next pass.
    uint8_t buffer[ESPMIDI_UART_RX_BYTES];
    size_t count = 0;
    while (count < sizeof(buffer) && serial_.available() > 0)
    {
      const int byte = serial_.read();
      if (byte < 0)
      {
        break;
      }
      buffer[count++] = static_cast<uint8_t>(byte);
    }

    if (count == 0)
    {
      return;
    }

    // The chunks handed to the router point into `buffer`, which is why the
    // router copies what it queues.
    parser_.parse(buffer, count, [this](const Message &message) { router_.receive(message); });
  }

  EndpointId endpoint() const { return endpoint_; }
  InPort in() const { return in_; }
  OutPort out() const { return out_; }
  bool started() const { return started_; }

private:
  static bool sendFrom(void *context, const Message &message)
  {
    return static_cast<BasicUartPort *>(context)->send(message);
  }

  bool send(const Message &message)
  {
    if (!started_)
    {
      return false;
    }
    return serializer_.serialize(
        message, [this](const uint8_t *bytes, size_t length) { return writeBytes(bytes, length); });
  }

  bool writeBytes(const uint8_t *bytes, size_t length)
  {
    return serial_.write(bytes, length) == length;
  }

  Router &router_;
  SerialType &serial_;
  uint8_t index_ = 0;
  EndpointId endpoint_;
  InPort in_;
  OutPort out_;
  Parser parser_;
  Serializer serializer_;
  bool started_ = false;
  // What the serial object was last opened on, so that begin() can tell a
  // reconfiguration from a move. -1 is "never set".
  int8_t rxPin_ = -1;
  int8_t txPin_ = -1;
};

#if defined(ARDUINO)
using UartPort = BasicUartPort<HardwareSerial>;
#endif

} // namespace espmidi

#endif // ESPMIDI_UART_H
