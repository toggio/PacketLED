/*
 * PacketLED - SyncBlink
 *
 * Both boards blink a Morse message on their PacketLED LEDs, in step.
 *
 * One board, the leader, sends a short "GO" packet. Both boards leave the
 * library calls at the same moment: the leader when endPacket() has read the
 * acknowledgement, the other board when parsePacket() has finished sending it.
 * From there both wait half a second, blink the message, pause, and the leader
 * starts a new round. While they blink, nobody transmits.
 *
 * Roles are automatic: a board that hears nothing for a few seconds becomes the
 * leader. Upload the same sketch to both boards.
 *
 * Wiring: LED_ANODE_PIN -> resistor -> LED anode, LED cathode -> LED_CATHODE_PIN
 * (see the README for the resistor value).
 * LED_ANODE_PIN must be an ADC1 pin. Place the two LEDs face to face.
 */

#include <PacketLED.h>

const uint8_t LED_ANODE_PIN = 32;
const uint8_t LED_CATHODE_PIN = 33;

const char MESSAGE[] = "SOS";        // letters, digits and spaces
const uint32_t UNIT_MS = 150;        // Morse unit: dot = 1, dash = 3
const uint32_t START_DELAY_MS = 500;
const uint32_t PAUSE_MS = 1500;      // between two rounds

ArduinoLedPhy phy(LED_ANODE_PIN, LED_CATHODE_PIN);
PacketLED led(phy);

bool leader = false;
uint32_t leadAfter = 0;              // become the leader if nothing is heard by then

const char *morse(char c) {
  static const char *const letters[] = {".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....", "..",
                                        ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.", "--.-", ".-.",
                                        "...",  "-",    "..-",  "...-", ".--",  "-..-", "-.--", "--.."};
  static const char *const digits[] = {"-----", ".----", "..---", "...--", "....-",
                                       ".....", "-....", "--...", "---..", "----."};
  if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
  if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
  if (c >= '0' && c <= '9') return digits[c - '0'];
  return nullptr;
}

// Sleeps until `at` (micros), then spins for the last couple of milliseconds.
void waitUntil(uint32_t at) {
  while ((int32_t)(at - micros()) > 2000) delay(1);
  while ((int32_t)(at - micros()) > 0) {
  }
}

// Blinks the message. Every edge is timed from t0, so the two boards stay in step.
// Returns the time the message ends.
uint32_t blinkMessage(uint32_t t0, bool lightOn) {
  const uint32_t unit = UNIT_MS * 1000;
  uint32_t t = t0 + START_DELAY_MS * 1000;
  for (const char *p = MESSAGE; *p; ++p) {
    if (*p == ' ') {
      t += 4 * unit;  // word gap: 7 units, 3 already added after the letter
      continue;
    }
    const char *code = morse(*p);
    if (!code) continue;
    for (const char *s = code; *s; ++s) {
      waitUntil(t);
      if (lightOn) led.setLed(true);
      t += (*s == '-' ? 3 : 1) * unit;
      waitUntil(t);
      led.setLed(false);
      t += unit;
    }
    t += 2 * unit;  // letter gap: 3 units
  }
  return t;
}

void scheduleLeaderTimeout() { leadAfter = millis() + 3000 + random(3000); }

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!led.begin()) {
    Serial.println("PacketLED init failed");
    while (true) delay(1000);
  }
  randomSeed(led.session());
  scheduleLeaderTimeout();
  Serial.printf("PacketLED SyncBlink - message \"%s\"\n", MESSAGE);
}

void loop() {
  if (leader) {
    led.beginPacket();
    led.print("GO");
    const bool ok = led.endPacket();
    const uint32_t t0 = micros();
    // After a retransmission the other board may have started on an earlier
    // copy: sit this round out, but keep the same timing.
    const bool inStep = ok && led.lastAttempts() == 1;
    Serial.println(ok ? (inStep ? "GO" : "GO (late, skipping this round)") : "GO not acknowledged");
    waitUntil(blinkMessage(t0, inStep));
    // Pause, listening in case another board also thinks it is the leader.
    const uint32_t pauseEnd = millis() + PAUSE_MS;
    while ((int32_t)(millis() - pauseEnd) < 0) {
      if (led.parsePacket() == 2 && led.peek() == 'G' && led.packetSession() > led.session()) {
        Serial.println("Another leader: following it");
        leader = false;
        scheduleLeaderTimeout();
        return;
      }
    }
    return;
  }

  if (led.parsePacket() == 2 && led.read() == 'G' && led.read() == 'O') {
    const uint32_t t0 = micros();
    Serial.println("GO received");
    waitUntil(blinkMessage(t0, true));
    scheduleLeaderTimeout();
    return;
  }

  if ((int32_t)(millis() - leadAfter) >= 0) {
    Serial.println("Nothing heard: leading");
    leader = true;
  }
}
