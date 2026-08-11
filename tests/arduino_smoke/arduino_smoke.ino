// EspMidi built as an Arduino library.
//
// unit/ compiles the core with the host g++ and never goes through arduino-cli,
// so it does not prove that the library resolves as an Arduino library: the
// library.properties layout, the include path, and the fact that the public
// header coexists with the Arduino core are all outside its reach. This sketch
// covers exactly that gap, which is why it stays small — the specification is
// fixed in unit/, not here.
//
// The default profile is the host core, so this runs in CI without a board. The
// same sketch runs on real hardware with --profile esp32s3; serial goes to UART0
// (the board's external USB-serial chip) rather than the native USB-OTG CDC,
// matching how the peer boards are wired.

#include <EspMidi.h>

namespace
{
int g_total = 0;

void expect(bool condition, const char *what)
{
  if (!condition)
  {
    Serial.print("TEST fail ");
    Serial.println(what);
    return;
  }
  g_total++;
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(100);

  int expected = 0;

  // The public header is reachable as <EspMidi.h> through library.properties.
  expect(ESPMIDI_VERSION_STR[0] != '\0', "version string is present");
  expected++;

  // The version header came along with it, without being included directly.
  expect(ESPMIDI_VERSION_MAJOR >= 0 && ESPMIDI_VERSION_MINOR >= 0 && ESPMIDI_VERSION_PATCH >= 0,
         "version macros are defined");
  expected++;

  Serial.print("TEST done ");
  Serial.print(g_total);
  Serial.print("/");
  Serial.println(expected);
}

void loop()
{
  delay(1000);
}
