// USB Host MIDI port, on EspUsbHost.
//
// Specification: docs/PORTS.ja.md.
//
// The first port whose endpoints are discovered rather than declared: MIDI
// devices come and go, and each connection becomes an endpoint with one port per
// cable. The sketch does not name them in advance — it watches the registry, or
// routes from InGroup::all() and lets whatever appears take part.
//
//   host task:    onMidiMessage --> a ring of raw packets
//   sketch task:  update() --> getDevices() diff --> seats
//                          --> the ring --> UsbPacketDecoder --> router.receive()
//
// **Everything that touches a seat runs on the sketch's task.** The library's
// callback arrives on EspUsbHost's own task, and all it does there is copy four
// bytes into a lock-free ring; the decoder, the cable map and the registry are
// only ever touched from update(). Connections are found by polling
// getDevices() rather than through connect callbacks for the same reason: it
// keeps the discovery on the side of the fence that owns the registry.
//
// **The cable counts are not inverted here.** EspUsbHost names them from the
// host's point of view and this library is the host, so they line up:
//
//   EspUsbHostMidiPortInfo::inCableCount   device -> host = we receive = InPort
//   EspUsbHostMidiPortInfo::outCableCount  host -> device = we send    = OutPort
//
// That is the opposite of EspMidiEspUsbDevice.h, where the same host-view names
// describe a device and therefore do invert. Worth reading both before changing
// either.
//
// Include this header only in a sketch that wants a USB Host port; EspMidi.h does
// not pull it in.

#ifndef ESPMIDI_ESP_USB_HOST_H
#define ESPMIDI_ESP_USB_HOST_H

#include "EspMidi.h"

#include <atomic>

#if defined(ARDUINO)
#include <EspUsbHost.h>
#endif

// Connected MIDI devices that can have seats at once. Each costs a decoder, an
// encoder per cable and the seats themselves, so the default is what a hub full
// of keyboards realistically needs rather than the registry's maximum.
#ifndef ESPMIDI_MAX_USB_HOST_DEVICES
#define ESPMIDI_MAX_USB_HOST_DEVICES 4
#endif

// Cables per connected device. USB MIDI 1.0 allows 16; a device with more than a
// handful is rare, and this bounds the per-device cost.
#ifndef ESPMIDI_USB_HOST_MAX_CABLES
#define ESPMIDI_USB_HOST_MAX_CABLES 8
#endif

// Packets the host task can leave for update() to decode. A bulk transfer can
// carry many, so this is sized for a burst rather than for one.
#ifndef ESPMIDI_USB_HOST_PACKETS
#define ESPMIDI_USB_HOST_PACKETS 64
#endif

// How often update() asks the library what is connected. Enumeration takes far
// longer than this, so a device is never noticeably late.
#ifndef ESPMIDI_USB_HOST_POLL_MS
#define ESPMIDI_USB_HOST_POLL_MS 100
#endif

namespace espmidi
{

// The two structures this port needs are deduced from the transport's own
// signatures rather than named, so nothing has to be agreed with it beyond the
// four methods called below. It is what lets a test on the host substitute a
// stand-in for EspUsbHost — and what keeps a sketch passing its own plain
// EspUsbHost object.
template <typename T, typename P>
P usbHostDeviceInfoType(size_t (T::*)(P *, size_t) const);

template <typename T, typename P>
P usbHostPortInfoType(bool (T::*)(P &, uint8_t) const);

template <typename HostType>
class BasicUsbHostPort
{
public:
  using DeviceInfo = decltype(usbHostDeviceInfoType(&HostType::getDevices));
  using MidiPortInfo = decltype(usbHostPortInfoType(&HostType::getMidiPortInfo));

  static constexpr uint8_t MaxCables =
      ESPMIDI_USB_HOST_MAX_CABLES < MaxPortsPerEndpoint ? ESPMIDI_USB_HOST_MAX_CABLES : MaxPortsPerEndpoint;

  explicit BasicUsbHostPort(Router &router, HostType &host) : router_(router), host_(host) {}

  // Registers the MIDI listener. Nothing is discovered here: seats appear from
  // update() as devices are found.
  bool begin()
  {
    if (started_)
    {
      return true;
    }
    started_ = true;
    // A generic lambda, so the message type never has to be named: it converts to
    // whatever callback signature the transport declares.
    host_.onMidiMessage([this](const auto &message) { onMidi(message); });
    return true;
  }

  // Marks every device's endpoint disconnected. The seats stay, so a sketch's
  // routes survive; what goes away is the connection.
  void end()
  {
    for (size_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES; i++)
    {
      if (devices_[i].used)
      {
        release(devices_[i]);
      }
    }
    started_ = false;
  }

  // Finds newly connected devices, drops the ones that went away, and decodes
  // whatever arrived. Call it from loop(), next to Router::update().
  void update(uint32_t nowMs)
  {
    if (!started_)
    {
      return;
    }

    // Discovery first, so a device that has just appeared already has its seats
    // when its packets are decoded below.
    if (!polled_ || static_cast<int32_t>(nowMs - nextPollMs_) >= 0)
    {
      polled_ = true;
      nextPollMs_ = nowMs + ESPMIDI_USB_HOST_POLL_MS;
      poll();
    }

    drain();
  }

#if defined(ARDUINO)
  void update() { update(millis()); }
#endif

  // The endpoint a connected device was given, or an invalid one.
  EndpointId endpointFor(uint8_t address) const
  {
    const DeviceSlot *slot = findByAddress(address);
    return slot ? slot->endpoint : EndpointId();
  }

  uint8_t inPortCount(uint8_t address) const
  {
    const DeviceSlot *slot = findByAddress(address);
    return slot ? slot->inPortCount : 0;
  }

  uint8_t outPortCount(uint8_t address) const
  {
    const DeviceSlot *slot = findByAddress(address);
    return slot ? slot->outPortCount : 0;
  }

  InPort in(uint8_t address, uint8_t cable = 0) const
  {
    const DeviceSlot *slot = findByAddress(address);
    return slot && cable < slot->inPortCount ? slot->inPorts[cable] : InPort();
  }

  OutPort out(uint8_t address, uint8_t cable = 0) const
  {
    const DeviceSlot *slot = findByAddress(address);
    return slot && cable < slot->outPortCount ? slot->outPorts[cable] : OutPort();
  }

  // The address of the nth connected device, or 0. Together with deviceCount()
  // this is enough to walk what is plugged in; a sketch that would rather be told
  // than ask listens to the registry instead (PortRegistry::addListener).
  uint8_t addressAt(size_t index) const
  {
    size_t seen = 0;
    for (size_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES; i++)
    {
      if (!devices_[i].used)
      {
        continue;
      }
      if (seen == index)
      {
        return devices_[i].address;
      }
      seen++;
    }
    return 0;
  }

  // Connected devices that currently have seats.
  size_t deviceCount() const
  {
    size_t count = 0;
    for (size_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES; i++)
    {
      count += devices_[i].used ? 1 : 0;
    }
    return count;
  }

  // --- Diagnostics --------------------------------------------------------

  // Packets that arrived on a cable the device did not declare.
  uint32_t unknownCablePackets() const { return unknownCable_; }
  // Packets dropped because update() had not caught up with the host task.
  uint32_t droppedPackets() const { return droppedPackets_.load(std::memory_order_relaxed); }
  // Devices refused because there was no room left for another one, either here
  // or in the registry. An unidentifiable device takes a fresh seat on every
  // connection (docs/PORTS.ja.md), so a rig that keeps swapping such devices runs
  // out eventually — this is how that becomes visible instead of mysterious.
  uint32_t refusedDevices() const { return refusedDevices_; }

private:
  struct CableContext
  {
    BasicUsbHostPort *self = nullptr;
    uint8_t device = 0;
    uint8_t cable = 0;
  };

  struct DeviceSlot
  {
    bool used = false;
    uint8_t address = 0;
    uint8_t inPortCount = 0;
    uint8_t outPortCount = 0;
    EndpointId endpoint;
    InPort inPorts[MaxCables];
    OutPort outPorts[MaxCables];
    UsbPacketDecoder decoder;
    UsbPacketEncoder encoders[MaxCables];
    CableContext contexts[MaxCables];
  };

  // One raw packet, as it came off the wire, plus the device it came from.
  struct RingEntry
  {
    uint8_t address = 0;
    uint8_t packet[UsbPacketBytes] = {};
    std::atomic<bool> ready{false};
  };

  static constexpr size_t EncodeBytes = ((ESPMIDI_CHUNK_BYTES + 6) / 3) * UsbPacketBytes;

  // --- The host task's half ----------------------------------------------

  // Runs on EspUsbHost's task. It copies and returns; every decision is left to
  // update(), which is the only thing allowed near a seat.
  template <typename MessageType>
  void onMidi(const MessageType &message)
  {
    if (!message.raw || message.length < UsbPacketBytes)
    {
      return;
    }

    uint32_t reserved = tail_.load(std::memory_order_relaxed);
    for (;;)
    {
      const uint32_t head = head_.load(std::memory_order_acquire);
      if (reserved - head >= ESPMIDI_USB_HOST_PACKETS)
      {
        droppedPackets_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      if (tail_.compare_exchange_weak(reserved, reserved + 1, std::memory_order_relaxed))
      {
        break;
      }
    }

    RingEntry &entry = ring_[reserved % ESPMIDI_USB_HOST_PACKETS];
    entry.address = message.address;
    for (size_t i = 0; i < UsbPacketBytes; i++)
    {
      entry.packet[i] = message.raw[i];
    }
    entry.ready.store(true, std::memory_order_release);
  }

  // --- The sketch task's half --------------------------------------------

  void drain()
  {
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    uint32_t head = head_.load(std::memory_order_relaxed);

    while (head != tail)
    {
      RingEntry &entry = ring_[head % ESPMIDI_USB_HOST_PACKETS];
      if (!entry.ready.load(std::memory_order_acquire))
      {
        break;
      }

      DeviceSlot *slot = findByAddress(entry.address);
      if (slot)
      {
        const uint8_t cable = static_cast<uint8_t>(entry.packet[0] >> 4);
        if (cable < slot->inPortCount)
        {
          slot->decoder.decodePacket(entry.packet, [this](const Message &message) { router_.receive(message); });
        }
        else
        {
          unknownCable_++;
        }
      }

      entry.ready.store(false, std::memory_order_relaxed);
      head++;
      head_.store(head, std::memory_order_release);
    }
  }

  void poll()
  {
    DeviceInfo found[ESPMIDI_MAX_USB_HOST_DEVICES];
    const size_t count = host_.getDevices(found, ESPMIDI_MAX_USB_HOST_DEVICES);

    // Anything with a seat that is no longer in the list has gone away.
    for (size_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES; i++)
    {
      DeviceSlot &slot = devices_[i];
      if (!slot.used)
      {
        continue;
      }
      bool present = false;
      for (size_t j = 0; j < count; j++)
      {
        present = present || found[j].address == slot.address;
      }
      if (!present)
      {
        release(slot);
      }
    }

    // Anything in the list with no seat yet may be a MIDI device. Whether it is
    // one is answered by the cable counts, which are only available once the
    // interface has been claimed — so a device that is not ready yet is simply
    // looked at again on the next poll.
    for (size_t j = 0; j < count; j++)
    {
      if (found[j].isHub || findByAddress(found[j].address))
      {
        continue;
      }
      adopt(found[j]);
    }
  }

  void adopt(const DeviceInfo &info)
  {
    MidiPortInfo ports;
    if (!host_.getMidiPortInfo(ports, info.address))
    {
      return; // not a MIDI device, or not claimed yet
    }

    DeviceSlot *slot = nullptr;
    for (size_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES; i++)
    {
      if (!devices_[i].used)
      {
        slot = &devices_[i];
        break;
      }
    }
    if (!slot)
    {
      refusedDevices_++;
      return;
    }

    EndpointIdentity identity;
    identity.transport = Transport::UsbHost;
    identity.index = 0;
    identity.vendorId = info.vid;
    identity.productId = info.pid;
    copySerial(identity.serial, info.serial);

    // The seat is matched on the identity, not the address: an address is
    // whatever the stack handed out this time round.
    const EndpointId endpoint = router_.registry().attachEndpoint(identity, deviceName(info));
    if (!endpoint.valid())
    {
      refusedDevices_++;
      return;
    }

    *slot = DeviceSlot();
    slot->used = true;
    slot->address = info.address;
    slot->endpoint = endpoint;

    // Not inverted: inCableCount is device to host, which is what we receive.
    slot->inPortCount = clampCables(ports.inCableCount);
    slot->outPortCount = clampCables(ports.outCableCount);

    const size_t index = static_cast<size_t>(slot - devices_);
    for (uint8_t cable = 0; cable < slot->inPortCount; cable++)
    {
      slot->inPorts[cable] = router_.registry().attachInPort(endpoint, cable);
      slot->decoder.setCablePort(cable, slot->inPorts[cable].port);
    }
    for (uint8_t cable = 0; cable < slot->outPortCount; cable++)
    {
      slot->outPorts[cable] = router_.registry().attachOutPort(endpoint, cable);
      slot->encoders[cable].setCable(cable);
      slot->contexts[cable].self = this;
      slot->contexts[cable].device = static_cast<uint8_t>(index);
      slot->contexts[cable].cable = cable;
      router_.setOutputSink(slot->outPorts[cable], &BasicUsbHostPort::sendFrom, &slot->contexts[cable]);
    }
  }

  void release(DeviceSlot &slot)
  {
    if (slot.endpoint.valid())
    {
      router_.registry().detachEndpoint(slot.endpoint);
    }
    // The seats keep pointing at this slot's sinks, and a send through them has
    // to fail rather than reach whatever device takes the address next. `used`
    // going false is what makes it fail.
    slot.used = false;
    slot.address = 0;
    slot.decoder.reset();
    for (uint8_t cable = 0; cable < slot.outPortCount; cable++)
    {
      slot.encoders[cable].reset();
    }
  }

  static uint8_t clampCables(uint8_t count) { return count > MaxCables ? MaxCables : count; }

  static void copySerial(char *dst, const char *src)
  {
    size_t i = 0;
    if (src)
    {
      for (; i + 1 < ESPMIDI_SERIAL_MAX && src[i] != '\0'; i++)
      {
        dst[i] = src[i];
      }
    }
    dst[i] = '\0';
  }

  static const char *deviceName(const DeviceInfo &info)
  {
    if (info.product && info.product[0] != '\0')
    {
      return info.product;
    }
    return "USB MIDI";
  }

  DeviceSlot *findByAddress(uint8_t address)
  {
    for (size_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES; i++)
    {
      if (devices_[i].used && devices_[i].address == address)
      {
        return &devices_[i];
      }
    }
    return nullptr;
  }

  const DeviceSlot *findByAddress(uint8_t address) const
  {
    for (size_t i = 0; i < ESPMIDI_MAX_USB_HOST_DEVICES; i++)
    {
      if (devices_[i].used && devices_[i].address == address)
      {
        return &devices_[i];
      }
    }
    return nullptr;
  }

  static bool sendFrom(void *context, const Message &message)
  {
    CableContext *cable = static_cast<CableContext *>(context);
    return cable->self->send(cable->device, cable->cable, message);
  }

  bool send(uint8_t device, uint8_t cable, const Message &message)
  {
    DeviceSlot &slot = devices_[device];
    if (!slot.used || cable >= slot.outPortCount)
    {
      return false;
    }

    uint8_t bytes[EncodeBytes] = {};
    const size_t length = slot.encoders[cable].encode(message, bytes, sizeof(bytes));
    if (length == 0)
    {
      return false;
    }

    // One transfer for the whole message: a dump split across several would let
    // another cable's packets land in the middle of it.
    return host_.midiSend(bytes, length, slot.address);
  }

  Router &router_;
  HostType &host_;
  DeviceSlot devices_[ESPMIDI_MAX_USB_HOST_DEVICES];
  RingEntry ring_[ESPMIDI_USB_HOST_PACKETS];
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
  std::atomic<uint32_t> droppedPackets_{0};
  uint32_t unknownCable_ = 0;
  uint32_t refusedDevices_ = 0;
  uint32_t nextPollMs_ = 0;
  bool polled_ = false;
  bool started_ = false;
};

#if defined(ARDUINO)
using UsbHostPort = BasicUsbHostPort<EspUsbHost>;
#endif

} // namespace espmidi

#endif // ESPMIDI_ESP_USB_HOST_H
