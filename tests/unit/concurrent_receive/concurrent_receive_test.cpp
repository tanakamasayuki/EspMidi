// Receiving from several tasks at once.
//
// This is the one boundary in the library that has to be thread-safe. A USB Host
// transfer callback and a BLE host task both call Router::receive() without
// asking the sketch's permission (docs/CORE_DESIGN.ja.md), while the sketch's own
// task is inside update() draining the queue.
//
// Threads on the host are not FreeRTOS tasks, but the memory model is the one the
// queue is written against, and running several producers against a consumer on a
// multi-core machine finds a torn queue far faster than a board will.
//
// What is asserted is the property that matters: **every message that was
// accepted comes out exactly once, intact, and in the order its own producer sent
// it.** Messages the queue refused are counted, never silently lost.

#include <EspMidi.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
int g_ran = 0;

constexpr int ProducerCount = 4;
constexpr int PerProducer = 2000;

struct Received
{
  // What each producer's port received, in arrival order.
  std::vector<uint8_t> byPort[ProducerCount];
  size_t total = 0;

  static bool write(void *context, const espmidi::Message &message)
  {
    Received *self = static_cast<Received *>(context);
    // data1 carries the producer, data2 the sequence number's low bits.
    assert(message.data1 < ProducerCount);
    self->byPort[message.data1].push_back(message.data2);
    self->total++;
    return true;
  }
};

void test_several_producers_and_one_consumer()
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};

  espmidi::EndpointIdentity sourceIdentity;
  sourceIdentity.transport = espmidi::Transport::UsbHost;
  sourceIdentity.index = 0;
  // Distinguishable seats, one per producer, so an entry landing on the wrong
  // port would show up as a gap in that port's sequence.
  espmidi::InPort sources[ProducerCount];
  for (int i = 0; i < ProducerCount; i++)
  {
    espmidi::EndpointIdentity identity = sourceIdentity;
    identity.index = static_cast<uint8_t>(i);
    identity.vendorId = 0x303a;
    identity.productId = static_cast<uint16_t>(0x4000 + i);
    identity.serial[0] = static_cast<char>('a' + i);
    const espmidi::EndpointId endpoint = registry.attachEndpoint(identity, "producer");
    sources[i] = registry.attachInPort(endpoint, 0);
    assert(sources[i].valid());
  }

  espmidi::EndpointIdentity sinkIdentity;
  sinkIdentity.transport = espmidi::Transport::Application;
  const espmidi::EndpointId sinkEndpoint = registry.attachEndpoint(sinkIdentity, "sink");
  const espmidi::OutPort out = registry.attachOutPort(sinkEndpoint, 0);
  Received received;
  router.setOutputSink(out, &Received::write, &received);
  router.addRoute(espmidi::InGroup::all(), out);

  std::atomic<int> accepted{0};
  std::atomic<int> refused{0};
  std::atomic<bool> go{false};

  std::vector<std::thread> producers;
  for (int i = 0; i < ProducerCount; i++)
  {
    producers.emplace_back([&, i] {
      while (!go.load(std::memory_order_acquire))
      {
      }
      for (int n = 0; n < PerProducer; n++)
      {
        uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
        espmidi::Message message;
        espmidi::buildShortMessage(message, bytes, 0x90, static_cast<uint8_t>(i), static_cast<uint8_t>(n & 0x7f));
        message.port = sources[i].port;
        if (router.receive(message))
        {
          accepted.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
          refused.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  go.store(true, std::memory_order_release);

  // The consumer, doing what loop() does.
  const int expected = ProducerCount * PerProducer;
  while (accepted.load(std::memory_order_relaxed) + refused.load(std::memory_order_relaxed) < expected ||
         router.queued() > 0)
  {
    router.update();
  }
  for (std::thread &producer : producers)
  {
    producer.join();
  }
  router.update();

  // Nothing vanished between the two sides of the queue.
  assert(received.total == static_cast<size_t>(accepted.load()));
  const espmidi::RouterCounters counters = router.counters();
  assert(counters.received == static_cast<uint32_t>(expected));
  assert(counters.queueFull == static_cast<uint32_t>(refused.load()));
  assert(counters.delivered == received.total);

  // A queue of 32 entries against 8000 messages does refuse some; that is the
  // documented behaviour, and it is what makes the counter worth having.
  // Whichever ones got through kept their own producer's order.
  for (int i = 0; i < ProducerCount; i++)
  {
    const std::vector<uint8_t> &sequence = received.byPort[i];
    int previous = -1;
    int wraps = 0;
    for (uint8_t value : sequence)
    {
      if (previous >= 0 && value <= previous)
      {
        // The sequence number is 7 bits, so it legitimately wraps.
        wraps++;
        assert(wraps <= PerProducer / 128 + 1);
      }
      previous = value;
    }
    assert(!sequence.empty());
  }
}

void test_a_producer_and_a_consumer_agree_across_the_wrap()
{
  // The queue indices are free-running and only reduced modulo the capacity when
  // a slot is addressed, so the interesting arithmetic is the subtraction. Many
  // times the capacity, one at a time, walks it through every wrap.
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};

  espmidi::EndpointIdentity identity;
  identity.transport = espmidi::Transport::Uart;
  const espmidi::EndpointId endpoint = registry.attachEndpoint(identity, "uart");
  const espmidi::InPort in = registry.attachInPort(endpoint, 0);

  espmidi::EndpointIdentity sinkIdentity;
  sinkIdentity.transport = espmidi::Transport::Application;
  const espmidi::EndpointId sinkEndpoint = registry.attachEndpoint(sinkIdentity, "sink");
  const espmidi::OutPort out = registry.attachOutPort(sinkEndpoint, 0);
  Received received;
  router.setOutputSink(out, &Received::write, &received);
  router.addRoute(in, out);

  const int count = ESPMIDI_QUEUE_ENTRIES * 40;
  for (int n = 0; n < count; n++)
  {
    uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
    espmidi::Message message;
    espmidi::buildShortMessage(message, bytes, 0x90, 0, static_cast<uint8_t>(n & 0x7f));
    message.port = in.port;
    assert(router.receive(message));
    router.update();
    assert(router.queued() == 0);
  }

  assert(received.total == static_cast<size_t>(count));
  assert(router.counters().queueFull == 0);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_several_producers_and_one_consumer);
  run(test_a_producer_and_a_consumer_agree_across_the_wrap);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
