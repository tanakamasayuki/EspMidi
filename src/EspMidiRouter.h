// Routing: routes, the three-stage pipeline, the queue that drives it, and the
// application port.
//
// Specification: docs/ROUTING.ja.md.
//
// The shape in one picture:
//
//   port adapter --receive()--> queue --update()--> in stage -> route -> out stage -> sink
//        (transport task)                (loop())
//
// Receiving runs in whatever context the transport uses — a USB Host task, the
// NimBLE host task — so nothing is sent from there. A received message is copied
// into the queue and the whole pipeline runs later from the sketch's update().
// That is what keeps the core free of the transports' threading.
//
// An application port is a port with no transport behind it: the sketch injects
// messages into it and receives messages from it. Monitors, control mapping
// helpers and anything that has to look at a message and decide are built on it,
// and it routes exactly like a UART or a USB cable — same seats, same groups,
// same loop rule.

#ifndef ESPMIDI_ROUTER_H
#define ESPMIDI_ROUTER_H

#include "EspMidiFilter.h"
#include "EspMidiMessage.h"
#include "EspMidiPort.h"

#include <atomic>

#ifndef ESPMIDI_MAX_ROUTES
#define ESPMIDI_MAX_ROUTES 16
#endif

#ifndef ESPMIDI_QUEUE_ENTRIES
#define ESPMIDI_QUEUE_ENTRIES 32
#endif

// Payload bytes one queued entry can carry. A larger chunk is split across
// several entries, which is safe because a chunk boundary carries no meaning of
// its own — only chunkStart and chunkEnd do.
#ifndef ESPMIDI_CHUNK_BYTES
#define ESPMIDI_CHUNK_BYTES 48
#endif

namespace espmidi
{

// What a transform stage decides about the message it was handed.
enum class Verdict : uint8_t
{
  Pass = 0,
  Drop,
};

// A stage of user code in the pipeline. It may inspect and modify the message in
// place, or drop it. To emit something different — one message becoming several,
// or a different kind of message — write to an application port instead; the
// send is queued, so it cannot recurse into the stage that made it.
//
// Chunks do not reach a transform. Interpreting a data stream is not this
// library's job, and re-deciding a route halfway through one would break the
// rule that a stream's path is fixed when it starts.
using TransformCallback = Verdict (*)(void *context, Message &message);

// How an output port actually sends. A port adapter registers one; returning
// false means the transport refused the message and is counted.
using OutputSink = bool (*)(void *context, const Message &message);

// What an application port hands the sketch.
using MessageCallback = void (*)(void *context, const Message &message);

struct Route
{
  static constexpr uint16_t Invalid = 0xffff;
  uint16_t value = Invalid;
  bool valid() const { return value != Invalid; }
};

inline bool operator==(const Route &a, const Route &b) { return a.value == b.value; }
inline bool operator!=(const Route &a, const Route &b) { return !(a == b); }

// Diagnostics. Every message the router does not deliver is counted somewhere,
// so "where did my note go" has an answer (REQUIREMENTS.ja.md, visibility).
struct RouterCounters
{
  uint32_t received = 0;          // handed in by a port adapter
  uint32_t queueFull = 0;         // dropped: the queue had no room
  uint32_t delivered = 0;         // written to a sink successfully
  uint32_t sendFailed = 0;        // a sink refused it
  uint32_t droppedByFilter = 0;   // a declarative filter rejected it
  uint32_t droppedByStage = 0;    // a transform could not represent it, or a callback said Drop
  uint32_t sysExRejected = 0;     // a second stream to a busy output
  uint32_t blockedBySysEx = 0;    // could not be sent during a stream
  uint32_t noRoute = 0;           // nothing was listening
};

class Router
{
public:
  static constexpr size_t MaxRoutes = ESPMIDI_MAX_ROUTES;
  static constexpr size_t MaxPorts = PortRegistry::MaxPorts;

  explicit Router(PortRegistry &registry) : registry_(registry)
  {
    // Rule 2 needs to know when an input goes away while a stream is open. The
    // event arrives in the transport's context, so it is only recorded here and
    // acted on from update().
    registry_.addListener(&Router::onPortEvent, this);
  }

  PortRegistry &registry() { return registry_; }
  const PortRegistry &registry() const { return registry_; }

  // --- Routes -------------------------------------------------------------

  Route addRoute(InPort from, OutPort to) { return add(from.port, false, to.port.value, false); }
  Route addRoute(InPort from, OutGroup to) { return add(from.port, false, to.value, true); }
  Route addRoute(InGroup from, OutPort to) { return add(PortId{from.value}, true, to.port.value, false); }
  Route addRoute(InGroup from, OutGroup to) { return add(PortId{from.value}, true, to.value, true); }

  bool removeRoute(Route route)
  {
    if (!validRoute(route))
    {
      return false;
    }
    routes_[route.value].used = false;
    return true;
  }

  bool setRouteEnabled(Route route, bool enabled)
  {
    if (!validRoute(route))
    {
      return false;
    }
    routes_[route.value].enabled = enabled;
    return true;
  }

  // Off by default: a message that came in on an endpoint is not sent back out
  // of it. Turning it on is for the rare setup that wants an echo.
  bool setRouteAllowSameEndpoint(Route route, bool allow)
  {
    if (!validRoute(route))
    {
      return false;
    }
    routes_[route.value].allowSameEndpoint = allow;
    return true;
  }

  // Each stage takes up to three rules, applied in this order:
  //   filter (declarative)  ->  transform (declarative)  ->  callback (user code)
  // A filter only narrows, a transform only rewrites, and a callback can do
  // both. Setting none of them leaves the stage transparent.
  bool setRouteFilter(Route route, const Filter &filter)
  {
    if (!validRoute(route))
    {
      return false;
    }
    routes_[route.value].rules.hasFilter = true;
    routes_[route.value].rules.filter = filter;
    return true;
  }

  bool setRouteTransform(Route route, const Transform &transform)
  {
    if (!validRoute(route))
    {
      return false;
    }
    routes_[route.value].rules.hasTransform = true;
    routes_[route.value].rules.transform = transform;
    return true;
  }

  bool setRouteCallback(Route route, TransformCallback callback, void *context = nullptr)
  {
    if (!validRoute(route))
    {
      return false;
    }
    routes_[route.value].rules.callback = callback;
    routes_[route.value].rules.context = context;
    return true;
  }

  size_t routeCount() const
  {
    size_t total = 0;
    for (size_t i = 0; i < routeSlots_; i++)
    {
      if (routes_[i].used)
      {
        total++;
      }
    }
    return total;
  }

  // --- Per-port stages ----------------------------------------------------

  bool setInPortFilter(InPort port, const Filter &filter) { return stageFilter(port.port, Direction::In, filter); }
  bool setInPortTransform(InPort port, const Transform &transform) { return stageTransform(port.port, Direction::In, transform); }
  bool setInPortCallback(InPort port, TransformCallback callback, void *context = nullptr)
  {
    return stageCallback(port.port, Direction::In, callback, context);
  }

  bool setOutPortFilter(OutPort port, const Filter &filter) { return stageFilter(port.port, Direction::Out, filter); }
  bool setOutPortTransform(OutPort port, const Transform &transform) { return stageTransform(port.port, Direction::Out, transform); }
  bool setOutPortCallback(OutPort port, TransformCallback callback, void *context = nullptr)
  {
    return stageCallback(port.port, Direction::Out, callback, context);
  }

  // --- Port adapter interface ---------------------------------------------

  // A port adapter registers how its output actually sends.
  bool setOutputSink(OutPort port, OutputSink sink, void *context = nullptr)
  {
    if (!port.valid() || port.port.value >= MaxPorts)
    {
      return false;
    }
    sinks_[port.port.value].sink = sink;
    sinks_[port.port.value].context = context;
    return true;
  }

  // A port adapter hands in a received message. Called from the transport's
  // context; the message is copied and nothing else happens until update().
  // Returns false when the queue is full, which is counted rather than blocking.
  //
  // **Safe to call from any task, and from several at once.** This is the one
  // place in the library that has to be: a USB Host transfer callback and a BLE
  // host task both arrive here without asking the sketch's permission
  // (docs/CORE_DESIGN.ja.md). Everything past the queue runs in update(), on the
  // sketch's task, single-threaded.
  bool receive(const Message &message)
  {
    received_.fetch_add(1, std::memory_order_relaxed);
    if (!message.port.valid())
    {
      return false;
    }

    if (!message.chunk)
    {
      return enqueue(message, nullptr, 0, message.chunkStart, message.chunkEnd);
    }

    // A chunk larger than one entry is split. Only the first keeps chunkStart
    // and only the last keeps chunkEnd, so the stream stays one stream.
    size_t offset = 0;
    bool ok = true;
    do
    {
      const size_t remaining = message.chunkLength - offset;
      const size_t take = remaining > ESPMIDI_CHUNK_BYTES ? ESPMIDI_CHUNK_BYTES : remaining;
      const bool first = offset == 0;
      const bool last = offset + take >= message.chunkLength;
      ok = enqueue(message,
                   message.chunkData ? &message.chunkData[offset] : nullptr,
                   take,
                   first && message.chunkStart,
                   last && message.chunkEnd) &&
           ok;
      offset += take;
    } while (offset < message.chunkLength);
    return ok;
  }

  // --- Driving ------------------------------------------------------------

  // Runs the pipeline for everything queued since the last call. Called from the
  // sketch's loop(); the core never drives itself.
  void update()
  {
    closePendingStreams();

    // Snapshotting the tail bounds the work to what was queued on entry, so a
    // stage that injects into an application port cannot keep this loop running
    // forever — and a transport that keeps receiving cannot either.
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    uint32_t head = head_.load(std::memory_order_relaxed);

    while (head != tail)
    {
      QueueEntry &entry = queue_[head % ESPMIDI_QUEUE_ENTRIES];
      if (!entry.ready.load(std::memory_order_acquire))
      {
        // Reserved by a producer that has not finished writing it. Waiting is
        // wrong here — this runs on the sketch's task — so the rest is left for
        // the next update(), a few microseconds away.
        break;
      }

      dispatch(entry);

      // The slot is released before the head moves, and the head is what a
      // producer checks for space, so a slot can never be overwritten while it
      // is being read.
      entry.ready.store(false, std::memory_order_relaxed);
      head++;
      head_.store(head, std::memory_order_release);
    }
  }

  // A snapshot rather than a reference: two of the counters are written from the
  // transport tasks, so they are read once here and handed over as plain values.
  RouterCounters counters() const
  {
    RouterCounters snapshot = counters_;
    snapshot.received = received_.load(std::memory_order_relaxed);
    snapshot.queueFull = queueFull_.load(std::memory_order_relaxed);
    return snapshot;
  }

  void resetCounters()
  {
    counters_ = RouterCounters();
    received_.store(0, std::memory_order_relaxed);
    queueFull_.store(0, std::memory_order_relaxed);
  }

  size_t queued() const
  {
    return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_relaxed);
  }

  // True while an output port is in the middle of a stream. A port adapter can
  // use it to decide whether a link may be torn down cleanly.
  bool outputBusy(OutPort port) const
  {
    return port.valid() && port.port.value < MaxPorts && outStreamOwner_[port.port.value].valid();
  }

private:
  // The rules one stage of the pipeline carries. Routes and ports use the same
  // structure so a rule behaves identically wherever it is placed.
  struct StageRules
  {
    bool hasFilter = false;
    Filter filter;
    bool hasTransform = false;
    Transform transform;
    TransformCallback callback = nullptr;
    void *context = nullptr;
  };

  struct RouteSlot
  {
    bool used = false;
    bool enabled = true;
    bool sourceIsGroup = false;
    bool destIsGroup = false;
    bool allowSameEndpoint = false;
    uint16_t source = 0; // PortId value, or InGroup value when sourceIsGroup
    uint16_t dest = 0;   // PortId value, or OutGroup value when destIsGroup
    StageRules rules;
  };

  struct SinkSlot
  {
    OutputSink sink = nullptr;
    void *context = nullptr;
  };

  struct QueueEntry
  {
    PortId port;
    MessageType type = MessageType::Midi1ChannelVoice;
    Timestamp timestamp;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint8_t dataLength = 0;
    bool chunk = false;
    bool chunkStart = false;
    bool chunkEnd = false;
    uint16_t chunkLength = 0;
    uint8_t chunkData[ESPMIDI_CHUNK_BYTES] = {};

    // Set once the entry is fully written. A slot is reserved before it is
    // filled, so the consumer needs to be told when the contents can be trusted.
    std::atomic<bool> ready{false};
  };

  // A set of ports, for remembering where an open stream is going.
  struct PortMask
  {
    uint32_t words[(MaxPorts + 31) / 32] = {};
    void clear()
    {
      for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
      {
        words[i] = 0;
      }
    }
    void set(uint16_t port) { words[port / 32] |= (1u << (port % 32)); }
    bool test(uint16_t port) const { return (words[port / 32] & (1u << (port % 32))) != 0; }
    bool any() const
    {
      for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
      {
        if (words[i] != 0)
        {
          return true;
        }
      }
      return false;
    }
  };

  struct StreamState
  {
    bool open = false;
    PortMask targets;
  };

  bool validRoute(Route route) const
  {
    return route.valid() && route.value < routeSlots_ && routes_[route.value].used;
  }

  Route add(PortId source, bool sourceIsGroup, uint16_t dest, bool destIsGroup)
  {
    if (!sourceIsGroup && !source.valid())
    {
      return Route();
    }
    size_t index = routeSlots_;
    for (size_t i = 0; i < routeSlots_; i++)
    {
      if (!routes_[i].used)
      {
        index = i;
        break;
      }
    }
    if (index == routeSlots_)
    {
      if (routeSlots_ >= MaxRoutes)
      {
        return Route();
      }
      routeSlots_++;
    }

    RouteSlot &slot = routes_[index];
    slot = RouteSlot();
    slot.used = true;
    slot.sourceIsGroup = sourceIsGroup;
    slot.destIsGroup = destIsGroup;
    slot.source = source.value;
    slot.dest = dest;
    return Route{static_cast<uint16_t>(index)};
  }

  StageRules *stageFor(PortId port, Direction direction)
  {
    if (!port.valid() || port.value >= MaxPorts || registry_.portDirection(port) != direction)
    {
      return nullptr;
    }
    return &stages_[port.value];
  }

  bool stageFilter(PortId port, Direction direction, const Filter &filter)
  {
    StageRules *rules = stageFor(port, direction);
    if (!rules)
    {
      return false;
    }
    rules->hasFilter = true;
    rules->filter = filter;
    return true;
  }

  bool stageTransform(PortId port, Direction direction, const Transform &transform)
  {
    StageRules *rules = stageFor(port, direction);
    if (!rules)
    {
      return false;
    }
    rules->hasTransform = true;
    rules->transform = transform;
    return true;
  }

  bool stageCallback(PortId port, Direction direction, TransformCallback callback, void *context)
  {
    StageRules *rules = stageFor(port, direction);
    if (!rules)
    {
      return false;
    }
    rules->callback = callback;
    rules->context = context;
    return true;
  }

  // Runs one stage. The filter is the only rule a data stream meets, and only at
  // its start: rule 1 fixes a stream's path when it begins, so re-deciding it
  // partway through is exactly what must not happen. Transforms and callbacks
  // never see a chunk at all.
  bool runStage(const StageRules &rules, Message &message)
  {
    const bool continuation = message.chunk && !message.chunkStart;
    if (!continuation && rules.hasFilter && !rules.filter.accepts(message))
    {
      counters_.droppedByFilter++;
      return false;
    }
    if (message.chunk)
    {
      return true;
    }
    if (rules.hasTransform && !rules.transform.apply(message))
    {
      counters_.droppedByStage++;
      return false;
    }
    if (rules.callback && rules.callback(rules.context, message) == Verdict::Drop)
    {
      counters_.droppedByStage++;
      return false;
    }
    return true;
  }

  bool enqueue(const Message &message, const uint8_t *chunkData, size_t chunkLength, bool start, bool end)
  {
    // Reserve a slot. Several transport tasks can be here at once, so the
    // reservation is a compare-and-swap rather than a write, and the index is
    // free-running: the subtraction below is correct across the wrap.
    uint32_t reserved = tail_.load(std::memory_order_relaxed);
    for (;;)
    {
      const uint32_t head = head_.load(std::memory_order_acquire);
      if (reserved - head >= ESPMIDI_QUEUE_ENTRIES)
      {
        queueFull_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }
      if (tail_.compare_exchange_weak(reserved, reserved + 1, std::memory_order_relaxed))
      {
        break;
      }
    }

    QueueEntry &entry = queue_[reserved % ESPMIDI_QUEUE_ENTRIES];
    entry.port = message.port;
    entry.type = message.type;
    entry.timestamp = message.timestamp;
    entry.status = message.status;
    entry.data1 = message.data1;
    entry.data2 = message.data2;
    entry.dataLength = message.dataLength;
    entry.chunk = message.chunk;
    entry.chunkStart = start;
    entry.chunkEnd = end;
    entry.chunkLength = static_cast<uint16_t>(chunkLength);
    for (size_t i = 0; i < chunkLength; i++)
    {
      entry.chunkData[i] = chunkData[i];
    }

    // Publishes everything written above.
    entry.ready.store(true, std::memory_order_release);
    return true;
  }

  static Message toMessage(const QueueEntry &entry)
  {
    Message message;
    message.port = entry.port;
    message.type = entry.type;
    message.timestamp = entry.timestamp;
    message.status = entry.status;
    message.data1 = entry.data1;
    message.data2 = entry.data2;
    message.dataLength = entry.dataLength;
    message.chunk = entry.chunk;
    message.chunkStart = entry.chunkStart;
    message.chunkEnd = entry.chunkEnd;
    if (entry.chunk)
    {
      message.chunkData = entry.chunkLength > 0 ? entry.chunkData : nullptr;
      message.chunkLength = entry.chunkLength;
    }
    return message;
  }

  void dispatch(const QueueEntry &entry)
  {
    Message message = toMessage(entry);
    const uint16_t source = message.port.value;
    if (source >= MaxPorts)
    {
      return;
    }

    // Stage 1: the input port's own rules.
    if (!runStage(stages_[source], message))
    {
      return;
    }

    // Rule 1: a stream's path is decided when it starts. Continuations replay
    // the destinations the start chose, so a routing change mid-dump cannot
    // split a stream between two sets of outputs.
    if (message.chunk && !message.chunkStart)
    {
      StreamState &stream = streams_[source];
      if (!stream.open)
      {
        return; // a continuation for a stream that was never started here
      }
      for (uint16_t out = 0; out < MaxPorts; out++)
      {
        if (stream.targets.test(out))
        {
          Message copy = message;
          deliver(PortId{out}, copy);
        }
      }
      if (message.chunkEnd)
      {
        stream.open = false;
        stream.targets.clear();
      }
      return;
    }

    PortMask started;
    bool anyDestination = false;

    for (size_t i = 0; i < routeSlots_; i++)
    {
      const RouteSlot &route = routes_[i];
      if (!route.used || !route.enabled || !routeAccepts(route, message.port))
      {
        continue;
      }

      Message routed = message;
      if (!runStage(route.rules, routed))
      {
        continue;
      }

      for (uint16_t out = 0; out < registry_.portCount() && out < MaxPorts; out++)
      {
        const PortId outPort{out};
        if (registry_.portDirection(outPort) != Direction::Out || !routeTargets(route, outPort))
        {
          continue;
        }
        // The default loop rule: not back out of the endpoint it came in on.
        if (!route.allowSameEndpoint && registry_.sameEndpoint(message.port, outPort))
        {
          continue;
        }

        anyDestination = true;
        Message copy = routed;
        if (deliver(outPort, copy) && copy.chunk && copy.chunkStart && !copy.chunkEnd)
        {
          started.set(out);
        }
      }
    }

    if (!anyDestination)
    {
      counters_.noRoute++;
      return;
    }

    if (message.chunk && message.chunkStart && !message.chunkEnd)
    {
      StreamState &stream = streams_[source];
      stream.open = started.any();
      stream.targets = started;
    }
  }

  bool routeAccepts(const RouteSlot &route, PortId port) const
  {
    if (!route.sourceIsGroup)
    {
      return route.source == port.value;
    }
    return registry_.groupContains(InGroup{route.source}, InPort{port});
  }

  bool routeTargets(const RouteSlot &route, PortId port) const
  {
    if (!route.destIsGroup)
    {
      return route.dest == port.value;
    }
    return registry_.groupContains(OutGroup{route.dest}, OutPort{port});
  }

  // Stage 3 and the actual send, with the SysEx exclusivity rule.
  bool deliver(PortId outPort, Message &message)
  {
    const uint16_t out = outPort.value;

    // Stage 3: the output port's own rules. Run before the exclusivity check so
    // a rejected stream never claims the output — it would then stay busy with a
    // stream that is not being sent.
    if (!runStage(stages_[out], message))
    {
      return false;
    }

    if (message.chunk)
    {
      if (message.chunkStart)
      {
        // Rule 3: one stream at a time per output. A second one is refused
        // rather than interleaved, which would corrupt both.
        if (outStreamOwner_[out].valid())
        {
          counters_.sysExRejected++;
          return false;
        }
        outStreamOwner_[out] = message.port;
      }
      else if (outStreamOwner_[out] != message.port)
      {
        return false;
      }
    }
    else if (outStreamOwner_[out].valid() && !isSystemRealTime(message.status))
    {
      // Nothing but System Real-Time may go out between an 0xF0 and its 0xF7,
      // so this message cannot be sent at all. It is dropped and counted rather
      // than delayed: holding it would need a per-output buffer and would let a
      // stalled dump silently reorder everything behind it.
      counters_.blockedBySysEx++;
      return false;
    }

    bool sent = false;
    if (sinks_[out].sink)
    {
      sent = sinks_[out].sink(sinks_[out].context, message);
    }

    if (sent)
    {
      counters_.delivered++;
    }
    else
    {
      counters_.sendFailed++;
    }

    if (message.chunk && message.chunkEnd && outStreamOwner_[out] == message.port)
    {
      outStreamOwner_[out] = PortId();
    }
    return sent;
  }

  // Rule 2: an input that goes away mid-stream leaves its outputs waiting for an
  // 0xF7 that is never coming, so one is sent for it.
  void closePendingStreams()
  {
    if (!pendingClose_.any())
    {
      return;
    }
    for (uint16_t in = 0; in < MaxPorts; in++)
    {
      if (!pendingClose_.test(in))
      {
        continue;
      }
      StreamState &stream = streams_[in];
      if (stream.open)
      {
        Message closing;
        closing.port = PortId{in};
        closing.type = MessageType::Data7;
        closing.status = 0xf0;
        closing.chunk = true;
        closing.chunkEnd = true;
        for (uint16_t out = 0; out < MaxPorts; out++)
        {
          if (stream.targets.test(out))
          {
            Message copy = closing;
            deliver(PortId{out}, copy);
          }
        }
        stream.open = false;
        stream.targets.clear();
      }
    }
    pendingClose_.clear();
  }

  static void onPortEvent(void *context, const PortEvent &event)
  {
    Router *router = static_cast<Router *>(context);
    if (event.type != PortEventType::PortStateChanged || event.state == PortState::Available)
    {
      return;
    }
    if (!event.port.valid() || event.port.value >= MaxPorts)
    {
      return;
    }
    // Recorded only. Sending from here would run in the transport's context,
    // which is exactly what the queue exists to avoid.
    if (router->streams_[event.port.value].open)
    {
      router->pendingClose_.set(event.port.value);
    }
    // An output that disappears mid-stream releases its exclusivity, so the next
    // stream is not blocked by a device that is gone.
    if (router->outStreamOwner_[event.port.value].valid())
    {
      router->outStreamOwner_[event.port.value] = PortId();
    }
  }

  PortRegistry &registry_;
  RouteSlot routes_[MaxRoutes];
  StageRules stages_[MaxPorts];
  SinkSlot sinks_[MaxPorts];
  StreamState streams_[MaxPorts];
  PortId outStreamOwner_[MaxPorts];
  PortMask pendingClose_;
  QueueEntry queue_[ESPMIDI_QUEUE_ENTRIES];
  RouterCounters counters_;
  size_t routeSlots_ = 0;

  // Free-running indices, not a head and a count: a count would have to be
  // written by both sides of the queue.
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
  std::atomic<uint32_t> received_{0};
  std::atomic<uint32_t> queueFull_{0};
};

// --- Application port -----------------------------------------------------

// A port the sketch drives. It is a normal endpoint with one input and one
// output, so it appears in groups, obeys the loop rule and can be routed like
// anything else — the sketch is a peer of the UART and the USB cable rather than
// a special case.
//
//   monitor:  router.addRoute(usbHostIn, app.out())  + app.onMessage(...)
//   generate: router.addRoute(app.in(), uartOut)     + app.send(...)
class AppPort
{
public:
  // `index` distinguishes several application ports from each other.
  AppPort(Router &router, const char *name = "application", uint8_t index = 0) : router_(router)
  {
    EndpointIdentity identity;
    identity.transport = Transport::Application;
    identity.index = index;
    endpoint_ = router.registry().attachEndpoint(identity, name);
    in_ = router.registry().attachInPort(endpoint_, 0);
    out_ = router.registry().attachOutPort(endpoint_, 0);
    router.setOutputSink(out_, &AppPort::sink, this);
  }

  EndpointId endpoint() const { return endpoint_; }
  InPort in() const { return in_; }
  OutPort out() const { return out_; }

  // Receives whatever routing delivers to this port. Runs inside update().
  void onMessage(MessageCallback callback, void *context = nullptr)
  {
    callback_ = callback;
    context_ = context;
  }

  // Injects a message. It is queued, so calling this from inside a transform or
  // from onMessage() cannot recurse: the injected message is handled by a later
  // update() rather than reentering the current one.
  bool send(const Message &message)
  {
    Message copy = message;
    copy.port = in_.port;
    return router_.receive(copy);
  }

  // Builds and injects a short message in one call, which is what a control
  // mapping helper needs.
  bool sendShort(uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0)
  {
    uint8_t bytes[MaxShortMessageBytes] = {};
    Message message;
    if (buildShortMessage(message, bytes, status, data1, data2) == 0)
    {
      return false;
    }
    return send(message);
  }

private:
  static bool sink(void *context, const Message &message)
  {
    AppPort *self = static_cast<AppPort *>(context);
    if (!self->callback_)
    {
      return false;
    }
    self->callback_(self->context_, message);
    return true;
  }

  Router &router_;
  EndpointId endpoint_;
  InPort in_;
  OutPort out_;
  MessageCallback callback_ = nullptr;
  void *context_ = nullptr;
};

} // namespace espmidi

#endif // ESPMIDI_ROUTER_H
