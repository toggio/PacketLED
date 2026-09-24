/*
 * PacketLED - BasicSend
 *
 * Sends a packet every 3 seconds, cycling through the different ways of
 * building one: text, raw bytes, a struct, and a packet without confirmation.
 * Run BasicReceive on the other board.
 *
 * Wiring: LED_ANODE_PIN -> resistor -> LED anode, LED cathode -> LED_CATHODE_PIN
 * (see the README for the resistor value).
 * LED_ANODE_PIN must be an ADC1 pin. Place the two LEDs face to face.
 */

#include <PacketLED.h>

const uint8_t LED_ANODE_PIN = 32;
const uint8_t LED_CATHODE_PIN = 33;

ArduinoLedPhy phy(LED_ANODE_PIN, LED_CATHODE_PIN);
PacketLED led(phy);

// Shared with BasicReceive: binary packets start with a non-printable tag byte,
// so the receiver can tell them from text.
const uint8_t TAG_BYTES = 0x01;
const uint8_t TAG_READING = 0x02;

struct __attribute__((packed)) Reading {
  uint32_t uptimeMs;
  int16_t temperatureX10;
  uint16_t light;
};

uint8_t step = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  // Same bit rate on both boards: begin() = 1024 bit/s, begin(512) or begin(256) for more range.
  if (!led.begin()) {
    Serial.println("PacketLED init failed");
    while (true) delay(1000);
  }
  Serial.printf("PacketLED BasicSend, session 0x%02X\n", led.session());
}

void loop() {
  delay(3000);
  bool ok = false;

  switch (step) {
    case 0:
      // Text: print() works like on Serial.
      led.beginPacket();
      led.print("Hello! uptime ");
      led.print(millis() / 1000);
      led.print(" s");
      ok = led.endPacket();
      Serial.printf("text      -> %s\n", ok ? "acknowledged" : "NOT acknowledged");
      break;

    case 1: {
      // Raw bytes, any value.
      const uint8_t data[] = {TAG_BYTES, 0x00, 0x01, 0x7F, 0x80, 0xFF, 0x42};
      led.beginPacket();
      led.write(data, sizeof(data));
      ok = led.endPacket();
      Serial.printf("bytes     -> %s\n", ok ? "acknowledged" : "NOT acknowledged");
      break;
    }

    case 2: {
      // A struct, sent as it is in memory. Both sides need the same definition.
      Reading r;
      r.uptimeMs = millis();
      r.temperatureX10 = 234;
      r.light = 512;
      led.beginPacket();
      led.write(TAG_READING);
      led.write((const uint8_t *)&r, sizeof(r));
      ok = led.endPacket();
      Serial.printf("struct    -> %s\n", ok ? "acknowledged" : "NOT acknowledged");
      break;
    }

    case 3:
      // No confirmation: sent once, faster, but delivery is not known.
      led.beginPacket();
      led.print("Unconfirmed");
      led.endPacket(false);
      Serial.println("no ACK    -> sent");
      break;
  }
  step = (step + 1) % 4;
}
