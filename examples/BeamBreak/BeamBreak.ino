/*
 * PacketLED - BeamBreak
 *
 * A light barrier made of two LEDs, with no data involved. The transmitter
 * blinks at 85 Hz; the receiver looks for that frequency only (synchronous
 * detection), so ambient light, mains hum and lamp flicker are ignored.
 *
 * Upload it to both boards and type 't' on one of them to make it the
 * transmitter. The other prints, five times a second:
 *
 *   amplitude  strength of the 85 Hz light from the transmitter
 *   noise      the same measure at 35 Hz, where nothing is sent
 *   window     integration time of each reading (it follows the ambient light)
 *
 * The beam counts as present when the amplitude is at least MIN_RATIO times
 * the noise. A longer BLOCK_MS makes the barrier more sensitive but slower to
 * react. BLINK_HZ and NOISE_HZ must stay away from 50/60 Hz, their harmonics
 * and each other.
 *
 * Wiring: LED_ANODE_PIN -> resistor -> LED anode, LED cathode -> LED_CATHODE_PIN
 * (see the README for the resistor value).
 * LED_ANODE_PIN must be an ADC1 pin that also works as an output (32/33 suit
 * the ESP32, 0/1 the ESP32-C3). Place the two LEDs face to face.
 */

#include <PacketLED.h>
#include <math.h>

const uint8_t LED_ANODE_PIN = 32;
const uint8_t LED_CATHODE_PIN = 33;

const float BLINK_HZ = 85;
const float NOISE_HZ = 35;
const uint32_t BLOCK_MS = 200;
const float MIN_RATIO = 5;

ArduinoLedPhy phy(LED_ANODE_PIN, LED_CATHODE_PIN);
PacketLED led(phy);

const uint16_t MAX_SAMPLES = 500;
uint16_t value[MAX_SAMPLES];
uint32_t when[MAX_SAMPLES];
bool transmitter = false;
uint32_t windowUs = 1000;

void blink() {
  static uint32_t next = 0;
  static bool on = false;
  const uint32_t halfPeriod = (uint32_t)(500000.0f / BLINK_HZ);
  const uint32_t now = micros();
  if ((int32_t)(now - next) >= 0) {
    next = now - next > halfPeriod ? now + halfPeriod : next + halfPeriod;  // restart if far behind
    on = !on;
    led.setLed(on);
  }
}

// Amplitude of frequency hz in the block, with a Hann window to keep
// neighbouring frequencies (mains hum) out.
float amplitude(uint16_t n, float mean, float hz) {
  const float span = (float)(when[n - 1] - when[0]);
  float re = 0, im = 0, weights = 0;
  for (uint16_t i = 0; i < n; ++i) {
    const float t = (float)(when[i] - when[0]);
    const float w = 0.5f - 0.5f * cosf(6.2831853f * t / span);
    const float a = 6.2831853f * hz * t * 1e-6f;
    const float v = (value[i] - mean) * w;
    re += v * cosf(a);
    im += v * sinf(a);
    weights += w;
  }
  return 2 * sqrtf(re * re + im * im) / weights;
}

void measure() {
  uint16_t n = 0, top = 0;
  uint32_t sum = 0;
  const uint32_t start = millis();
  while (millis() - start < BLOCK_MS && n < MAX_SAMPLES) {
    when[n] = micros();
    value[n] = led.measureLight(windowUs);
    sum += value[n];
    if (value[n] > top) top = value[n];
    ++n;
  }
  if (n < 20) return;
  const float mean = (float)sum / n;
  const float amp = amplitude(n, mean, BLINK_HZ);
  const float noise = amplitude(n, mean, NOISE_HZ);
  const bool beam = amp >= MIN_RATIO * noise && amp >= 5;
  Serial.printf("amplitude %6.1f   noise %5.1f   window %4lu us   %s\n", amp, noise, (unsigned long)windowUs,
                top >= 4090 ? "SATURATED" : beam ? "BEAM OK" : "BEAM BROKEN");

  // Keep the readings well inside the ADC range.
  if (top >= 3500 && windowUs > 20) windowUs /= 2;
  else if (top < 1000 && windowUs < 2000) windowUs *= 2;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!led.begin()) {
    Serial.println("PacketLED init failed");
    while (true) delay(1000);
  }
  Serial.println("PacketLED BeamBreak - type 't' to make this board the transmitter");
}

void loop() {
  if (Serial.available() && Serial.read() == 't') {
    transmitter = !transmitter;
    led.setLed(false);
    Serial.println(transmitter ? "Transmitter: blinking at 85 Hz" : "Receiver");
  }
  if (transmitter) blink();
  else measure();
}
