/*
 * PacketLED - BasicReceive
 *
 * Receives the packets sent by BasicSend and prints them. Acknowledgements are
 * sent automatically and duplicates are discarded.
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

// Shared with BasicSend.
const uint8_t TAG_BYTES = 0x01;
const uint8_t TAG_READING = 0x02;

struct __attribute__((packed)) Reading {
  uint32_t uptimeMs;
  int16_t temperatureX10;
  uint16_t light;
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!led.begin()) {  // same bit rate as the sender
    Serial.println("PacketLED init failed");
    while (true) delay(1000);
  }
  Serial.println("PacketLED BasicReceive, listening");
}

void loop() {
  const int n = led.parsePacket();
  if (n <= 0) return;

  uint8_t data[lx25::kMaxPayload];
  led.readBytes(data, n);
  Serial.printf("#%u from 0x%02X, %d bytes, level %u: ", led.packetSeq(), led.packetSession(), n,
                led.packetLevel());

  if (n == 1 + (int)sizeof(Reading) && data[0] == TAG_READING) {
    Reading r;
    memcpy(&r, data + 1, sizeof(r));
    Serial.printf("struct uptime=%lu ms temperature=%.1f light=%u\n", (unsigned long)r.uptimeMs,
                  r.temperatureX10 / 10.0, r.light);
  } else if (data[0] == TAG_BYTES) {
    Serial.print("bytes");
    for (int i = 1; i < n; ++i) Serial.printf(" %02X", data[i]);
    Serial.println();
  } else {
    // Text arrives without a terminating '\0'.
    char text[lx25::kMaxPayload + 1];
    memcpy(text, data, n);
    text[n] = '\0';
    Serial.printf("text \"%s\"\n", text);
  }
}
