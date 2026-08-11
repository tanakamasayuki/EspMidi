// The public header must compile for the host on its own.
//
// This is not only a version check. It is compiled with the host g++, with no
// Arduino core, no Arduino.h and no ESP-IDF on the include path, so it fails the
// moment the core starts depending on any of them. That dependency rule is what
// lets every other unit test run without an Arduino toolchain
// (docs/CORE_DESIGN.ja.md).

#include <EspMidi.h>

#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
  int total = 0;

  // The version header is reachable through the public header alone.
  assert(ESPMIDI_VERSION_MAJOR >= 0);
  assert(ESPMIDI_VERSION_MINOR >= 0);
  assert(ESPMIDI_VERSION_PATCH >= 0);
  ++total;

  // ESPMIDI_VERSION_STR agrees with the numeric macros, so a hand-edited header
  // (rather than one written by tools/bump_version.py) is caught here.
  char expected[32];
  std::snprintf(expected,
                sizeof(expected),
                "%d.%d.%d",
                ESPMIDI_VERSION_MAJOR,
                ESPMIDI_VERSION_MINOR,
                ESPMIDI_VERSION_PATCH);
  assert(std::strcmp(expected, ESPMIDI_VERSION_STR) == 0);
  ++total;

  std::printf("TEST done %d/%d\n", total, total);
  return 0;
}
