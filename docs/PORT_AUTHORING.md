# Writing a port

[日本語](PORT_AUTHORING.ja.md)

**Ports are header-only, so one can live outside this repository.** RTP-MIDI, an SPI bridge, a CV/Gate converter, a transport that does not exist yet — any of them can take part on the same terms as the bundled ports.

This document is the contract. [`src/EspMidiUart.h`](../src/EspMidiUart.h) is the smallest bundled port, so reading it alongside this helps.

## What a port does

**Four things.**

1. **Supplies seats** — registers an endpoint and its ports with the `PortRegistry`.
2. **Hands over what it receives** — calls `Router::receive()`.
3. **Takes on sending** — writes to the wire from the function registered with `setOutputSink()`.
4. **Reflects the state** — tells the registry when the link comes and goes.

**Nothing else.** Routing, filtering, the queue, the loop rule and the three SysEx rules are all the core's job.

## The skeleton

```cpp
#include "EspMidi.h"

namespace espmidi
{

template <typename TransportType>
class BasicMyPort
{
public:
  BasicMyPort(Router &router, TransportType &transport, uint8_t index = 0)
      : router_(router), transport_(transport), index_(index) {}

  bool begin(const char *name = "My MIDI")
  {
    EndpointIdentity identity;
    identity.transport = Transport::Uart;   // pick the closest one
    identity.index = index_;

    endpoint_ = router_.registry().attachEndpoint(identity, name);
    if (!endpoint_.valid()) return false;

    in_ = router_.registry().attachInPort(endpoint_, 0);
    out_ = router_.registry().attachOutPort(endpoint_, 0);
    if (!in_.valid() || !out_.valid()) return false;

    return router_.setOutputSink(out_, &BasicMyPort::sendFrom, this);
  }

  void update()
  {
    // read the wire, make a Message, hand it to router_.receive()
  }

  InPort in() const { return in_; }
  OutPort out() const { return out_; }

private:
  static bool sendFrom(void *context, const Message &message)
  {
    return static_cast<BasicMyPort *>(context)->send(message);
  }

  bool send(const Message &message)
  {
    // write to the wire; false if it was refused
  }

  Router &router_;
  TransportType &transport_;
  uint8_t index_ = 0;
  EndpointId endpoint_;
  InPort in_;
  OutPort out_;
};

#if defined(ARDUINO)
using MyPort = BasicMyPort<RealTransport>;
#endif

} // namespace espmidi
```

## The rules to follow

### 1. Do not own the stack

**Never `begin()` or `end()` it.** Take the transport the sketch started **by reference**. That is what lets MIDI coexist with HID, CDC and custom GATT services ([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md), Japanese).

### 2. Make `begin()` idempotent

Reconfiguring should be "call `begin()` again". `attachEndpoint()` and `attachInPort()` are idempotent, so **the same arguments return the same seats**. Never create a new seat: the sketch's routes would keep pointing at the old one.

**When the arguments change, release what you were holding first.** The seats are idempotent; **the peripheral underneath is not**. An ESP32 reaches a pad through the GPIO matrix, so opening a second pin does not take the first one back and the board keeps driving a pin nobody asked for. `espmidi::UartPort::begin()` closes an already-open `HardwareSerial` before opening it again.

### 3. Never remove a seat

On disconnect, call `detachEndpoint()` and **change only the state to `Disconnected`**. There is no API in `PortRegistry` to remove a seat. That is the whole basis of "unplugging does not mean rebuilding routes" ([DATA_MODEL.md](DATA_MODEL.md)).

### 4. Match dynamic ports on an identity

For a transport where devices come and go, put **something that will match again on reconnect** into `EndpointIdentity`.

| Usable | Never |
| --- | --- |
| VID / PID / serial, a MAC address, a fixed URL | **a number assigned this time round** (a USB address, a connection id) |

A device that cannot be identified gets **a fresh seat every time**. That is the correct behaviour: it stops one device inheriting another's routing.

### 5. Receiving may happen on any task

`Router::receive()` is **the one thread-safe entry point**. It can be called straight from a transport's callback.

**But do nothing else there.** Creating a seat, reading the registry, sending — all of that belongs to `update()`, on the sketch's task. Keep the shared state as small as possible.

What the bundled ports do:

| Port | What happens in the callback |
| --- | --- |
| UART / USB Device | polled, so there is no other task to begin with |
| USB Host | **copies the raw four bytes into a lock-free ring, and nothing else.** Decoding happens in `update()` |
| BLE | **calls `receive()` directly.** All it shares is one port handle per connection |

BLE can call it directly because the queue does the copying, so **a dump is not copied**. USB Host needs a ring because `EspUsbHost` hands over one packet at a time in its own buffer. **The shape of the transport changes the answer.**

### 6. Handing over pointers is fine

`Message::raw` and `chunkData` may **point straight into the transport's buffer**. `receive()` copies into the queue, so nothing has to outlive the call. **This is what lets a long SysEx cross without being copied.**

### 7. Return false when you cannot send

Not connected, FIFO full, out of range — return `false` from the sink. The core counts it in `sendFailed`. **Never drop it silently.**

### 8. Bound how much you read

Always cap what one `update()` reads. A device streaming a dump that holds `loop()` stalls every other port.

```cpp
#ifndef ESPMIDI_MYPORT_RX_BYTES
#define ESPMIDI_MYPORT_RX_BYTES 64
#endif
```

What is left unread stays in the transport's buffer for the next `update()`.

### 9. Expose diagnostics

If your port can discard something the core's counters cannot explain, **count it yourself and publish it**.

```cpp
uint32_t unknownCablePackets() const;   // arrived on a cable never declared
uint32_t droppedPackets() const;        // the ring overflowed
uint32_t refusedDevices() const;        // out of seats
```

Whether "it does not work" can be diagnosed is decided here.

### 10. Close an interrupted stream

If the other side disappears mid-send, **send an `0xF7` first if you can** (rule 2), then close. If you cannot, just fold the state so that **the next dump is not a continuation of the last one**.

## Making it testable

**Make the transport a template parameter.** Every bundled port does.

```cpp
template <typename TransportType> class BasicMyPort { ... };
#if defined(ARDUINO)
using MyPort = BasicMyPort<RealTransport>;
#endif
```

Then a stand-in lets it run on the host. **Supplying seats, receiving reaching the router, framing, what happens when full, how a disconnect is handled** — all of it can be fixed without a board.

```cpp
struct FakeTransport {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  // only the methods the port calls
};
using TestPort = espmidi::BasicMyPort<FakeTransport>;
```

Then **what is left for hardware is only what hardware can show**. For the bundled ports, that turned out to be "the bytes really cross the pad" and nothing more ([../tests/unit/README.ja.md](../tests/unit/README.ja.md), Japanese).

### Avoiding naming the transport's types

Not having to name the transport's structures makes a test's stand-in much easier. The bundled ports use two techniques.

**Deduce from a signature:**

```cpp
template <typename T, typename P>
P deviceInfoType(size_t (T::*)(P *, size_t) const);

using DeviceInfo = decltype(deviceInfoType(&HostType::getDevices));
```

**Take it with a generic lambda:**

```cpp
transport_.onMessage([this](const auto &message) { onMessage(message); });
```

## What not to do

| | Why |
| --- | --- |
| Send to another port from a sink | that is routing's job, and the loop rule stops applying |
| Send from inside `receive()` | it means touching another transport from this one's task |
| Start your own task | the core does not. `loop()` drives everything ([CORE_DESIGN.ja.md](CORE_DESIGN.ja.md)) |
| Interpret SysEx contents | device-specific; carry it |
| Read a timestamp and reorder | carried, never interpreted |
| Remove a seat | it breaks routes |
| Use the heap | storage is fixed-size and tunable through `ESPMIDI_*` |

## Checklist

- [ ] takes the stack by reference and never `begin()`s or `end()`s it
- [ ] `begin()` is idempotent and returns the same seats
- [ ] a disconnect changes the state and does not remove the seat
- [ ] if dynamic, matches on an identity and takes a new seat when it cannot
- [ ] nothing but `receive()` is called from another task
- [ ] returns `false` when it cannot send
- [ ] `update()` bounds how much it reads
- [ ] counts and exposes its own discards
- [ ] closes an interrupted stream
- [ ] is a template, with tests on the host
- [ ] fixed-size storage, with limits changeable by `#define`

## What to read, in order

1. [`src/EspMidiUart.h`](../src/EspMidiUart.h) — the smallest. One static pair of seats.
2. [`src/EspMidiEspUsbDevice.h`](../src/EspMidiEspUsbDevice.h) — a seat per cable, and mount state.
3. [`src/EspMidiEspBle.h`](../src/EspMidiEspBle.h) — `receive()` from another task, and dump reassembly.
4. [`src/EspMidiEspUsbHost.h`](../src/EspMidiEspUsbHost.h) — dynamic seats, a ring, and discovery by polling.

The tests are in the same order, starting at `tests/unit/uart_port`.
