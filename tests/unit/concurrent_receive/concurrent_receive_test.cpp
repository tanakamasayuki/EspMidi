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
// accepted comes out exactly once, and in the order its own producer sent it.**
// Messages the queue refused are counted, never silently lost.
//
// What is deliberately **not** asserted is fairness. A slot is reserved with a
// compare-and-swap, so a thread that keeps losing that race keeps being refused —
// with a full queue, one producer can get nothing through at all. That has been
// measured, and it is written down here rather than papered over.

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

// Runs `perProducer` messages from each of ProducerCount threads through the queue
// while the calling thread drains it, and returns what came out.
struct Run
{
  Received received;
  int accepted = 0;
  int refused = 0;
  espmidi::RouterCounters counters;
};

void drive(int perProducer, Run &result)
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};

  // Distinguishable seats, one per producer, so an entry landing on the wrong port
  // would show up in that port's own sequence.
  espmidi::InPort sources[ProducerCount];
  for (int i = 0; i < ProducerCount; i++)
  {
    espmidi::EndpointIdentity identity;
    identity.transport = espmidi::Transport::UsbHost;
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
  router.setOutputSink(out, &Received::write, &result.received);
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
      for (int n = 0; n < perProducer; n++)
      {
        uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
        espmidi::Message message;
        // data1 carries the producer, data2 its sequence number.
        espmidi::buildShortMessage(message, bytes, 0x90, static_cast<uint8_t>(i), static_cast<uint8_t>(n));
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
  const int expected = ProducerCount * perProducer;
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

  result.accepted = accepted.load();
  result.refused = refused.load();
  result.counters = router.counters();
}

void test_nothing_is_lost_between_the_two_sides()
{
  // The property that matters: whatever was accepted comes out, and whatever was
  // refused is counted. Nothing falls into the gap.
  Run run;
  drive(PerProducer, run);
  const int expected = ProducerCount * PerProducer;

  assert(run.accepted + run.refused == expected);
  assert(run.received.total == static_cast<size_t>(run.accepted));
  assert(run.counters.received == static_cast<uint32_t>(expected));
  assert(run.counters.queueFull == static_cast<uint32_t>(run.refused));
  assert(run.counters.delivered == run.received.total);
  assert(run.received.total > 0);
}

void test_the_queue_is_not_fair_when_it_is_full()
{
  // Deliberately recorded rather than assumed away. A slot is reserved with a
  // compare-and-swap, and a thread that keeps losing that race keeps being
  // refused: with a full queue **one producer can get nothing through at all**,
  // and it has been measured doing so.
  //
  // It matters because two transports can be receiving at once — a USB Host task
  // and a BLE task — and when the queue is full, one of them can be starved. The
  // answer is a deeper queue or a more frequent update(), not an expectation of
  // fairness. Nothing here asserts a per-producer share, and nothing anywhere else
  // should either.
  Run run;
  drive(PerProducer, run);

  // What is promised instead: the accounting above, and that the queue recovers.
  // Every producer finished, and the queue is empty at the end.
  assert(run.accepted + run.refused == ProducerCount * PerProducer);
}

void test_each_producer_keeps_its_own_order()
{
  // Ordering is promised per producer, not across producers: two transport tasks
  // have no order relative to each other in the first place.
  //
  // Few enough messages that a 7-bit sequence number does not wrap, so this is an
  // exact check rather than a modular one — a queue that swapped two entries would
  // fail it.
  Run run;
  drive(100, run);

  size_t seen = 0;
  for (int i = 0; i < ProducerCount; i++)
  {
    const std::vector<uint8_t> &sequence = run.received.byPort[i];
    seen += sequence.size();
    for (size_t n = 1; n < sequence.size(); n++)
    {
      assert(sequence[n] > sequence[n - 1]);
    }
  }
  assert(seen == run.received.total);
  assert(run.received.total > 0);
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
  run(test_nothing_is_lost_between_the_two_sides);
  run(test_the_queue_is_not_fair_when_it_is_full);
  run(test_each_producer_keeps_its_own_order);
  run(test_a_producer_and_a_consumer_agree_across_the_wrap);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
