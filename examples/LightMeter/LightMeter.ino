/*
 * PacketLED - LightMeter
 *
 * Shows how much light one board receives from the other, with the same
 * integration times the library uses. Handy for choosing LEDs, resistor and
 * distance before sending any data.
 *
 * Upload it to both boards. On one of them type 't': its LED starts blinking
 * (25 ms on, 25 ms off). The other board prints, four times a second:
 *
 *   light  reading while the other LED is on
 *   dark   reading while it is off
 *   level  light - dark: the signal the protocol works with
 *   noise  spread of the dark readings (mains hum, ADC noise)
 *
 * The link is comfortable when level is well above noise and the readings are
 * not saturated.
 *
 * Keys: t = blink / stop blinking, 1 5 2 = integration time of 1024 / 512 / 256 bit/s,
 *       + - = double / halve the integration time.
 *
 * Wiring: LED_ANODE_PIN -> resistor -> LED anode, LED cathode -> LED_CATHODE_PIN
 * (see the README for the resistor value).
 * LED_ANODE_PIN must be an ADC1 pin that also works as an output (32/33 suit
 * the ESP32, 0/1 the ESP32-C3). Place the two LEDs face to face.
 */

#include <PacketLED.h>

const uint8_t LED_ANODE_PIN = 32;
const uint8_t LED_CATHODE_PIN = 33;

ArduinoLedPhy phy(LED_ANODE_PIN, LED_CATHODE_PIN);
PacketLED led(phy);

const uint16_t MAX_SAMPLES = 600;
const uint32_t MAX_WINDOW_US = 20000;
uint16_t samples[MAX_SAMPLES];
bool blinking = false;
uint32_t windowUs = 0;

void printWindow() { Serial.printf("Integration %lu us\n", (unsigned long)windowUs); }

void setRate(uint32_t rate) {
  if (!led.begin(rate)) {
    Serial.println("PacketLED init failed");
    return;
  }
  windowUs = led.maxWindowUs();
  Serial.printf("Integration %lu us (as at %lu bit/s)\n", (unsigned long)windowUs, (unsigned long)rate);
}

void blink() {
  static uint32_t last = 0;
  static bool on = false;
  if (millis() - last >= 25) {
    last = millis();
    on = !on;
    led.setLed(on);
  }
}

// Samples for 250 ms and splits the readings into a bright and a dark group
// around the midpoint. Medians and percentiles keep the few readings that
// straddle an edge of the blinking LED out of the result.
void measure() {
  uint16_t n = 0;
  const uint32_t start = millis();
  while (millis() - start < 250 && n < MAX_SAMPLES) samples[n++] = led.measureLight(windowUs);
  if (n < 10) return;

  for (uint16_t i = 1; i < n; ++i)  // sort
    for (uint16_t j = i; j > 0 && samples[j] < samples[j - 1]; --j) {
      const uint16_t t = samples[j];
      samples[j] = samples[j - 1];
      samples[j - 1] = t;
    }
  const uint16_t mid = (samples[0] + samples[n - 1]) / 2;
  uint16_t nDark = 0;
  while (nDark < n && samples[nDark] <= mid) ++nDark;
  const uint16_t nLight = n - nDark;

  const uint16_t dark = samples[nDark / 2];
  const uint16_t light = nLight ? samples[nDark + nLight / 2] : dark;
  const uint16_t noise = samples[nDark * 9 / 10] - samples[nDark / 10];
  const bool seen = nLight > n / 5 && nDark > n / 5 && light - dark > 3 * (noise + 1);
  const char *note = samples[n - 1] >= 4090 ? "SATURATED: shorter window ('-')" : seen ? "" : "(no blinking LED seen)";
  Serial.printf("light %4u  dark %4u  level %4u  noise %3u  %s\n", light, dark, light - dark, noise, note);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("PacketLED LightMeter - keys: t 1 5 2 + -");
  setRate(1024);
}

void loop() {
  if (Serial.available()) {
    switch (Serial.read()) {
      case 't':
        blinking = !blinking;
        led.setLed(false);
        Serial.println(blinking ? "Blinking" : "Measuring");
        break;
      case '1': setRate(1024); break;
      case '5': setRate(512); break;
      case '2': setRate(256); break;
      case '+':
        if (windowUs * 2 <= MAX_WINDOW_US) windowUs *= 2;
        printWindow();
        break;
      case '-':
        if (windowUs > 10) windowUs /= 2;
        printWindow();
        break;
      default: break;
    }
  }
  if (blinking) blink();
  else measure();
}
