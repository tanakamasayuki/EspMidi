// USB Device MIDI port, on EspUsbDevice.
//
// Specification: docs/PORTS.ja.md.
//
// The board appears to a PC as a MIDI interface with as many ports as the sketch
// declared cables for. Nothing here builds descriptors or touches USB: the sketch
// owns the EspUsbDevice stack and hands this port an EspUsbDeviceMidi that is
// already configured (docs/CORE_DESIGN.ja.md).
//
//   tud_midi packets --> UsbPacketDecoder --> router.receive()   (queued)
//   tud_midi packets <-- UsbPacketEncoder <-- the output sinks
//
// The packet codec itself is core (EspMidiUsbPacket.h) and shared with the USB
// Host port, so what is left here is the cable-to-seat mapping and the mount
// state.
//
// **The two cable counts are named from the host's point of view**, as USB
// endpoint directions and EspUsbHost's port info are. They are therefore the
// opposite way round from this library's ports:
//
//   EspUsbDeviceMidi::outCableCount()  host -> device  = what we receive = InPort
//   EspUsbDeviceMidi::inCableCount()   device -> host  = what we send    = OutPort
//
// Getting that backwards produces a device whose ports all work in the wrong
// direction, which is why the accessors here are named after this library's own
// directions instead: inPortCount() / outPortCount().
//
// Include this header only in a sketch that wants a USB Device port; EspMidi.h
// does not pull it in, so a sketch with no USB device does not require
// EspUsbDevice to be installed.

#ifndef ESPMIDI_ESP_USB_DEVICE_H
#define ESPMIDI_ESP_USB_DEVICE_H

#include "EspMidi.h"

#if defined(ARDUINO)
#include <EspUsbDevice.h>
#endif

// Packets read from the USB endpoint in one update(). A host sending a patch dump
// must not be able to hold loop(); what is not read now is still in TinyUSB's
// FIFO for the next pass.
#ifndef ESPMIDI_USB_PACKETS_PER_UPDATE
#define ESPMIDI_USB_PACKETS_PER_UPDATE 32
#endif

namespace espmidi
{

// Deduces the packet type from the transport's own signature, so this port needs
// no agreement with it beyond the two methods it calls. It is what lets a test on
// the host substitute a stand-in for EspUsbDeviceMidi.
template <typename T, typename P>
P usbDevicePacketType(bool (T::*)(const P &));

template <typename MidiType, typename DeviceType>
class BasicUsbDevicePort
{
public:
  using Packet = decltype(usbDevicePacketType(&MidiType::writePacket));

  // `device` is asked whether the host has the device configured; `midi` supplies
  // the cables and carries the packets. `index` distinguishes this endpoint from
  // other USB Device endpoints and is what a seat is matched on.
  BasicUsbDevicePort(Router &router, MidiType &midi, DeviceType &device, uint8_t index = 0)
      : router_(router), midi_(midi), device_(device), index_(index)
  {
  }

  // Supplies one endpoint with one port per declared cable. Call it after the USB
  // stack has been started, so the cable counts are final. Idempotent.
  bool begin(const char *name = "USB MIDI")
  {
    EndpointIdentity identity;
    identity.transport = Transport::UsbDevice;
    identity.index = index_;

    endpoint_ = router_.registry().attachEndpoint(identity, name);
    if (!endpoint_.valid())
    {
      return false;
    }

    // A cable count of 0 means the device has no ports in that direction. It is
    // not the same as one port, so nothing is created for it.
    inPortCount_ = clampCables(midi_.outCableCount());
    outPortCount_ = clampCables(midi_.inCableCount());

    for (uint8_t cable = 0; cable < inPortCount_; cable++)
    {
      inPorts_[cable] = router_.registry().attachInPort(endpoint_, cable);
      if (!inPorts_[cable].valid())
      {
        return false;
      }
      decoder_.setCablePort(cable, inPorts_[cable].port);
    }

    for (uint8_t cable = 0; cable < outPortCount_; cable++)
    {
      outPorts_[cable] = router_.registry().attachOutPort(endpoint_, cable);
      if (!outPorts_[cable].valid())
      {
        return false;
      }
      encoders_[cable].setCable(cable);
      contexts_[cable].self = this;
      contexts_[cable].cable = cable;
      if (!router_.setOutputSink(outPorts_[cable], &BasicUsbDevicePort::sendFrom, &contexts_[cable]))
      {
        return false;
      }
    }

    // The host decides when the device is usable, so the endpoint starts
    // disconnected and update() reports the state it finds.
    available_ = false;
    router_.registry().detachEndpoint(endpoint_);
    return true;
  }

  // Marks the endpoint disconnected and drops any half-decoded stream. The seats
  // stay: it is the connection that goes away, not the ports.
  void end()
  {
    resetStreams();
    available_ = false;
    if (endpoint_.valid())
    {
      router_.registry().detachEndpoint(endpoint_);
    }
  }

  // Follows the mount state and reads whatever the host has sent. Call it from
  // loop(), next to EspUsbDevice::task() and Router::update().
  void update()
  {
    if (!endpoint_.valid())
    {
      return;
    }

    const bool up = device_.ready();
    if (up != available_)
    {
      available_ = up;
      if (up)
      {
        // attachEndpoint() is idempotent, so a remount is the same call again and
        // returns the same seats with every route still pointing at them.
        EndpointIdentity identity;
        identity.transport = Transport::UsbDevice;
        identity.index = index_;
        router_.registry().attachEndpoint(identity);
      }
      else
      {
        // Whatever was in flight cannot be finished. It is dropped rather than
        // terminated, because there is nobody left to send an 0xF7 to.
        resetStreams();
        router_.registry().detachEndpoint(endpoint_);
      }
    }

    if (!available_)
    {
      return;
    }

    for (size_t i = 0; i < ESPMIDI_USB_PACKETS_PER_UPDATE; i++)
    {
      Packet packet;
      if (!midi_.readPacket(packet))
      {
        break;
      }

      const uint8_t bytes[UsbPacketBytes] = {packet.header, packet.byte1, packet.byte2, packet.byte3};

      // A packet on a cable the device never declared is dropped: the decoder
      // leaves its port invalid, and routing has nowhere to take it. Counted
      // because a host doing this means the two sides disagree about the
      // descriptor, which is worth being able to see.
      if (cableOf(bytes[0]) >= inPortCount_)
      {
        unknownCable_++;
        continue;
      }

      decoder_.decodePacket(bytes, [this](const Message &message) { router_.receive(message); });
    }
  }

  EndpointId endpoint() const { return endpoint_; }
  bool available() const { return available_; }

  // Ports this library receives on, i.e. the host's OUT cables.
  uint8_t inPortCount() const { return inPortCount_; }
  // Ports this library sends on, i.e. the host's IN cables.
  uint8_t outPortCount() const { return outPortCount_; }

  InPort in(uint8_t cable = 0) const { return cable < inPortCount_ ? inPorts_[cable] : InPort(); }
  OutPort out(uint8_t cable = 0) const { return cable < outPortCount_ ? outPorts_[cable] : OutPort(); }

  // Packets that arrived on a cable this device never declared.
  uint32_t unknownCablePackets() const { return unknownCable_; }

private:
  struct CableContext
  {
    BasicUsbDevicePort *self = nullptr;
    uint8_t cable = 0;
  };

  // Two bytes may be pending, the 0xF0 / 0xF7 framing adds one at each end, and
  // the result is rounded up to whole packets — the same arithmetic as
  // UsbPacketEncoder::maxEncodedBytes() for the largest chunk the queue carries.
  static constexpr size_t EncodeBytes = ((ESPMIDI_CHUNK_BYTES + 6) / 3) * UsbPacketBytes;

  static uint8_t cableOf(uint8_t header) { return static_cast<uint8_t>(header >> 4); }

  static uint8_t clampCables(uint8_t count)
  {
    return count > MaxPortsPerEndpoint ? MaxPortsPerEndpoint : count;
  }

  static bool sendFrom(void *context, const Message &message)
  {
    CableContext *cable = static_cast<CableContext *>(context);
    return cable->self->send(cable->cable, message);
  }

  bool send(uint8_t cable, const Message &message)
  {
    if (!available_ || cable >= outPortCount_)
    {
      return false;
    }

    uint8_t bytes[EncodeBytes] = {};
    const size_t length = encoders_[cable].encode(message, bytes, sizeof(bytes));
    if (length == 0)
    {
      return false;
    }

    // One packet at a time, and a refusal stops there: the transport's FIFO is
    // full, and pushing the rest of a dump into it would leave the stream broken
    // in the middle rather than at a packet boundary.
    for (size_t i = 0; i < length; i += UsbPacketBytes)
    {
      Packet packet;
      packet.header = bytes[i + 0];
      packet.byte1 = bytes[i + 1];
      packet.byte2 = bytes[i + 2];
      packet.byte3 = bytes[i + 3];
      if (!midi_.writePacket(packet))
      {
        return false;
      }
    }
    return true;
  }

  void resetStreams()
  {
    decoder_.reset();
    for (uint8_t cable = 0; cable < outPortCount_; cable++)
    {
      encoders_[cable].reset();
    }
    // reset() clears the cable map along with the SysEx state, so the seats have
    // to be handed back to the decoder.
    for (uint8_t cable = 0; cable < inPortCount_; cable++)
    {
      decoder_.setCablePort(cable, inPorts_[cable].port);
    }
  }

  Router &router_;
  MidiType &midi_;
  DeviceType &device_;
  uint8_t index_ = 0;
  EndpointId endpoint_;
  uint8_t inPortCount_ = 0;
  uint8_t outPortCount_ = 0;
  InPort inPorts_[MaxPortsPerEndpoint];
  OutPort outPorts_[MaxPortsPerEndpoint];
  UsbPacketDecoder decoder_;
  UsbPacketEncoder encoders_[MaxPortsPerEndpoint];
  CableContext contexts_[MaxPortsPerEndpoint];
  uint32_t unknownCable_ = 0;
  bool available_ = false;
};

#if defined(ARDUINO)
using UsbDevicePort = BasicUsbDevicePort<EspUsbDeviceMidi, EspUsbDevice>;
#endif

} // namespace espmidi

#endif // ESPMIDI_ESP_USB_DEVICE_H
