// Routing: routes, the three-stage pipeline, queue-driven delivery, the loop
// rule, the application port and the diagnostic counters.
//
// Specification: docs/ROUTING.ja.md. The SysEx stream rules have their own
// subject in unit/sysex_rules.

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
int g_ran = 0;

// A stand-in for a port adapter's transport: records what was asked of it and
// can be told to refuse, the way a disconnected link would.
struct Sink
{
  static constexpr size_t Capacity = 32;
  uint8_t status[Capacity] = {};
  uint8_t data1[Capacity] = {};
  uint8_t data2[Capacity] = {};
  size_t count = 0;
  bool accept = true;

  static bool write(void *context, const espmidi::Message &message)
  {
    Sink *self = static_cast<Sink *>(context);
    if (!self->accept)
    {
      return false;
    }
    assert(self->count < Capacity);
    self->status[self->count] = message.status;
    self->data1[self->count] = message.data1;
    self->data2[self->count] = message.data2;
    self->count++;
    return true;
  }

  void clear() { count = 0; }
};

espmidi::EndpointIdentity uart(uint8_t index)
{
  espmidi::EndpointIdentity identity;
  identity.transport = espmidi::Transport::Uart;
  identity.index = index;
  return identity;
}

// A registry, a router and two UART-like endpoints, which is the shape most of
// these tests need.
struct Fixture
{
  espmidi::PortRegistry registry;
  espmidi::Router router{registry};
  espmidi::EndpointId endpointA;
  espmidi::EndpointId endpointB;
  espmidi::InPort inA;
  espmidi::OutPort outA;
  espmidi::InPort inB;
  espmidi::OutPort outB;
  Sink sinkA;
  Sink sinkB;

  Fixture()
  {
    endpointA = registry.attachEndpoint(uart(1), "UART1");
    endpointB = registry.attachEndpoint(uart(2), "UART2");
    inA = registry.attachInPort(endpointA, 0);
    outA = registry.attachOutPort(endpointA, 0);
    inB = registry.attachInPort(endpointB, 0);
    outB = registry.attachOutPort(endpointB, 0);
    router.setOutputSink(outA, &Sink::write, &sinkA);
    router.setOutputSink(outB, &Sink::write, &sinkB);
  }

  bool receive(espmidi::InPort port, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0)
  {
    uint8_t bytes[espmidi::MaxShortMessageBytes] = {};
    espmidi::Message message;
    espmidi::buildShortMessage(message, bytes, status, d1, d2);
    message.port = port.port;
    return router.receive(message);
  }
};

void test_nothing_is_sent_before_update()
{
  // Receiving runs in the transport's context, so it may only queue. Sending
  // from there would reach another transport from the wrong task.
  Fixture f;
  f.router.addRoute(f.inA, f.outB);

  f.receive(f.inA, 0x90, 60, 100);
  assert(f.sinkB.count == 0);
  assert(f.router.queued() == 1);

  f.router.update();
  assert(f.sinkB.count == 1);
  assert(f.sinkB.status[0] == 0x90 && f.sinkB.data1[0] == 60);
  assert(f.router.queued() == 0);
}

void test_one_to_many_and_many_to_one()
{
  Fixture f;
  // One input to two outputs.
  f.router.addRoute(f.inA, f.outB);
  const espmidi::EndpointId c = f.registry.attachEndpoint(uart(3), "UART3");
  const espmidi::OutPort outC = f.registry.attachOutPort(c, 0);
  Sink sinkC;
  f.router.setOutputSink(outC, &Sink::write, &sinkC);
  f.router.addRoute(f.inA, outC);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 1 && sinkC.count == 1);

  // Two inputs into one output.
  f.sinkB.clear();
  f.router.addRoute(f.inB, f.outA);
  const espmidi::InPort inC = f.registry.attachInPort(c, 0);
  f.router.addRoute(inC, f.outA);

  f.receive(f.inB, 0x90, 62, 100);
  f.receive(inC, 0x90, 64, 100);
  f.router.update();
  assert(f.sinkA.count == 2);
  assert(f.sinkA.data1[0] == 62 && f.sinkA.data1[1] == 64);
}

void test_no_route_means_no_delivery()
{
  Fixture f;
  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkA.count == 0 && f.sinkB.count == 0);
  assert(f.router.counters().noRoute == 1);
}

void test_same_endpoint_is_refused_by_default()
{
  // A message that came in on an endpoint is not sent back out of it: a
  // keyboard would hear its own notes returned.
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.inA, f.outA);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkA.count == 0);
  assert(f.router.counters().noRoute == 1);

  // The rare setup that wants the echo can ask for it.
  f.router.setRouteAllowSameEndpoint(route, true);
  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkA.count == 1);
}

void test_groups_as_route_endpoints()
{
  Fixture f;
  const espmidi::OutGroup synths = f.registry.addOutGroup("synths");
  f.registry.addToGroup(synths, f.outB);
  f.router.addRoute(f.inA, synths);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 1);

  // A route to every output picks up a port that appears later, because the
  // reserved group is not a list anyone maintains.
  Fixture g;
  g.router.addRoute(g.inA, espmidi::OutGroup::all());
  const espmidi::EndpointId c = g.registry.attachEndpoint(uart(3), "UART3");
  const espmidi::OutPort outC = g.registry.attachOutPort(c, 0);
  Sink sinkC;
  g.router.setOutputSink(outC, &Sink::write, &sinkC);

  g.receive(g.inA, 0x90, 60, 100);
  g.router.update();
  assert(g.sinkB.count == 1);
  assert(sinkC.count == 1);
  // Still not back out of its own endpoint, even through "all outputs".
  assert(g.sinkA.count == 0);
}

espmidi::Verdict transposeUp(void *context, espmidi::Message &message)
{
  (void)context;
  if (message.command() == 0x90 || message.command() == 0x80)
  {
    message.data1 = static_cast<uint8_t>((message.data1 + 12) & 0x7f);
  }
  return espmidi::Verdict::Pass;
}

espmidi::Verdict dropAll(void *context, espmidi::Message &message)
{
  (void)context;
  (void)message;
  return espmidi::Verdict::Drop;
}

espmidi::Verdict countCalls(void *context, espmidi::Message &message)
{
  (void)message;
  (*static_cast<int *>(context))++;
  return espmidi::Verdict::Pass;
}

void test_three_pipeline_stages()
{
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.inA, f.outB);

  int inCalls = 0;
  int outCalls = 0;
  f.router.setInPortCallback(f.inA, &countCalls, &inCalls);
  f.router.setRouteCallback(route, &transposeUp);
  f.router.setOutPortCallback(f.outB, &countCalls, &outCalls);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();

  assert(inCalls == 1);
  assert(outCalls == 1);
  assert(f.sinkB.count == 1);
  assert(f.sinkB.data1[0] == 72); // the route's transform ran

  // A stage may only be set on a port of the matching direction.
  assert(!f.router.setInPortCallback(espmidi::InPort{f.outB.port}, &countCalls, &inCalls));
}

void test_a_stage_can_drop()
{
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.inA, f.outB);
  f.router.setRouteCallback(route, &dropAll);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 0);
  assert(f.router.counters().droppedByStage == 1);
}

void test_route_transforms_do_not_leak_between_routes()
{
  // Each route sees the message as it arrived. One route transposing must not
  // change what another route delivers.
  Fixture f;
  const espmidi::Route toB = f.router.addRoute(f.inA, f.outB);
  f.router.setRouteCallback(toB, &transposeUp);

  const espmidi::EndpointId c = f.registry.attachEndpoint(uart(3), "UART3");
  const espmidi::OutPort outC = f.registry.attachOutPort(c, 0);
  Sink sinkC;
  f.router.setOutputSink(outC, &Sink::write, &sinkC);
  f.router.addRoute(f.inA, outC);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();

  assert(f.sinkB.data1[0] == 72);
  assert(sinkC.data1[0] == 60);
}

void test_routes_run_in_registration_order()
{
  Fixture f;
  const espmidi::EndpointId c = f.registry.attachEndpoint(uart(3), "UART3");
  const espmidi::OutPort outC = f.registry.attachOutPort(c, 0);
  Sink shared;
  f.router.setOutputSink(f.outB, &Sink::write, &shared);
  f.router.setOutputSink(outC, &Sink::write, &shared);

  const espmidi::Route first = f.router.addRoute(f.inA, f.outB);
  f.router.addRoute(f.inA, outC);
  f.router.setRouteCallback(first, &transposeUp);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();

  assert(shared.count == 2);
  assert(shared.data1[0] == 72); // the first route registered
  assert(shared.data1[1] == 60);
}

void test_enable_and_remove()
{
  Fixture f;
  const espmidi::Route route = f.router.addRoute(f.inA, f.outB);
  assert(f.router.routeCount() == 1);

  f.router.setRouteEnabled(route, false);
  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 0);

  f.router.setRouteEnabled(route, true);
  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 1);

  assert(f.router.removeRoute(route));
  assert(f.router.routeCount() == 0);
  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 1);

  // A removed route's slot is reused rather than leaked.
  assert(f.router.addRoute(f.inA, f.outB).valid());
  assert(f.router.routeCount() == 1);
}

void test_send_failure_is_counted()
{
  Fixture f;
  f.router.addRoute(f.inA, f.outB);
  f.sinkB.accept = false;

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 0);
  assert(f.router.counters().sendFailed == 1);
  assert(f.router.counters().delivered == 0);
}

void test_queue_overflow_is_counted_not_blocking()
{
  Fixture f;
  f.router.addRoute(f.inA, f.outB);

  for (size_t i = 0; i < ESPMIDI_QUEUE_ENTRIES; i++)
  {
    assert(f.receive(f.inA, 0x90, 60, 100));
  }
  // One more than the queue holds: refused, counted, and the earlier messages
  // are untouched.
  assert(!f.receive(f.inA, 0x90, 60, 100));
  assert(f.router.counters().queueFull == 1);

  f.router.update();
  assert(f.sinkB.count == ESPMIDI_QUEUE_ENTRIES);
}

struct Received
{
  static constexpr size_t Capacity = 16;
  uint8_t status[Capacity] = {};
  uint8_t data1[Capacity] = {};
  size_t count = 0;

  static void handle(void *context, const espmidi::Message &message)
  {
    Received *self = static_cast<Received *>(context);
    assert(self->count < Capacity);
    self->status[self->count] = message.status;
    self->data1[self->count] = message.data1;
    self->count++;
  }
};

void test_app_port_observes()
{
  // A monitor is a route to an application port; nothing about the sketch is a
  // special case in the pipeline.
  Fixture f;
  espmidi::AppPort app(f.router, "monitor");
  Received received;
  app.onMessage(&Received::handle, &received);
  f.router.addRoute(f.inA, app.out());

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();

  assert(received.count == 1);
  assert(received.status[0] == 0x90 && received.data1[0] == 60);
}

void test_app_port_injects()
{
  Fixture f;
  espmidi::AppPort app(f.router, "generator");
  f.router.addRoute(app.in(), f.outA);

  assert(app.sendShort(0xb0, 7, 64));
  assert(f.sinkA.count == 0); // queued like anything else
  f.router.update();

  assert(f.sinkA.count == 1);
  assert(f.sinkA.status[0] == 0xb0 && f.sinkA.data1[0] == 7);
}

struct Rewriter
{
  espmidi::AppPort *app = nullptr;
  int seen = 0;

  static void handle(void *context, const espmidi::Message &message)
  {
    Rewriter *self = static_cast<Rewriter *>(context);
    self->seen++;
    // Look at the message and emit a different one: the case a transform cannot
    // express, because one message becomes another kind of message.
    if (message.command() == 0x90)
    {
      self->app->sendShort(0xb0, 7, message.data2);
    }
  }
};

void test_app_port_can_look_and_emit_something_else()
{
  Fixture f;
  espmidi::AppPort watcher(f.router, "watcher", 0);
  espmidi::AppPort emitter(f.router, "emitter", 1);
  Rewriter rewriter;
  rewriter.app = &emitter;
  watcher.onMessage(&Rewriter::handle, &rewriter);

  f.router.addRoute(f.inA, watcher.out());
  f.router.addRoute(emitter.in(), f.outB);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(rewriter.seen == 1);
  // The injected message is queued, so it lands on the next pass rather than
  // recursing into this one.
  assert(f.sinkB.count == 0);

  f.router.update();
  assert(f.sinkB.count == 1);
  assert(f.sinkB.status[0] == 0xb0 && f.sinkB.data2[0] == 100);
}

struct Echo
{
  espmidi::AppPort *app = nullptr;
  int seen = 0;

  static void handle(void *context, const espmidi::Message &message)
  {
    Echo *self = static_cast<Echo *>(context);
    self->seen++;
    self->app->send(message);
  }
};

void test_app_feedback_cannot_recurse_within_one_update()
{
  // The sketch wires its own output back into its own input. Each update()
  // handles one lap and stops, so a feedback loop cannot hang the pipeline.
  Fixture f;
  espmidi::AppPort a(f.router, "a", 0);
  espmidi::AppPort b(f.router, "b", 1);
  Echo echo;
  echo.app = &b;
  a.onMessage(&Echo::handle, &echo);

  f.router.addRoute(f.inA, a.out());
  f.router.addRoute(b.in(), a.out());

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(echo.seen == 1);
  f.router.update();
  assert(echo.seen == 2);
  f.router.update();
  assert(echo.seen == 3);
  // Each pass terminates; nothing spins.
}

void test_app_port_is_a_normal_port()
{
  Fixture f;
  espmidi::AppPort app(f.router, "app");

  espmidi::PortInfo info;
  assert(f.registry.portInfo(app.in(), info));
  assert(info.transport == espmidi::Transport::Application);
  assert(std::strcmp(info.name, "app") == 0);
  assert(info.state == espmidi::PortState::Available);

  // It is in the reserved groups like any other port.
  assert(f.registry.groupContains(espmidi::InGroup::all(), app.in()));
  assert(f.registry.groupContains(espmidi::OutGroup::all(), app.out()));

  // Its own input and output share an endpoint, so the loop rule stops a
  // sketch from wiring itself into a tight circle by accident.
  assert(f.registry.sameEndpoint(app.in(), app.out()));
  f.router.addRoute(app.in(), app.out());
  app.sendShort(0x90, 60, 100);
  f.router.update();
  assert(f.router.counters().noRoute == 1);
}

void test_disconnected_output_still_routes_and_counts()
{
  // A route pointing at an unplugged device is not an error; the send simply
  // fails and is counted. Unplugging must not require reconfiguring.
  Fixture f;
  f.router.addRoute(f.inA, f.outB);
  f.sinkB.accept = false;
  f.registry.detachEndpoint(f.endpointB);

  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.router.counters().sendFailed == 1);
  assert(f.router.routeCount() == 1);

  // Back again, and the same route delivers without being touched.
  f.sinkB.accept = true;
  f.registry.attachEndpoint(uart(2), "UART2");
  f.receive(f.inA, 0x90, 60, 100);
  f.router.update();
  assert(f.sinkB.count == 1);
}

void run(void (*test)())
{
  test();
  g_ran++;
}
} // namespace

int main()
{
  run(test_nothing_is_sent_before_update);
  run(test_one_to_many_and_many_to_one);
  run(test_no_route_means_no_delivery);
  run(test_same_endpoint_is_refused_by_default);
  run(test_groups_as_route_endpoints);
  run(test_three_pipeline_stages);
  run(test_a_stage_can_drop);
  run(test_route_transforms_do_not_leak_between_routes);
  run(test_routes_run_in_registration_order);
  run(test_enable_and_remove);
  run(test_send_failure_is_counted);
  run(test_queue_overflow_is_counted_not_blocking);
  run(test_app_port_observes);
  run(test_app_port_injects);
  run(test_app_port_can_look_and_emit_something_else);
  run(test_app_feedback_cannot_recurse_within_one_update);
  run(test_app_port_is_a_normal_port);
  run(test_disconnected_output_still_routes_and_counts);

  std::printf("TEST done %d/%d\n", g_ran, g_ran);
  return 0;
}
