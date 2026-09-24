/*
 * PacketLED - Chat
 *
 * A two-way text chat between two boards. Upload this sketch to both, open a
 * Serial Monitor on each (115200 baud, line ending "Newline") and type.
 * Lines longer than 64 characters are cut.
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

char line[lx25::kMaxPayload];
size_t lineLen = 0;
bool lineTooLong = false;

void sendLine() {
  Serial.print("> ");
  Serial.write((const uint8_t *)line, lineLen);
  if (lineTooLong) Serial.print(" [cut]");
  led.beginPacket();
  led.write((const uint8_t *)line, lineLen);
  Serial.println(led.endPacket() ? "" : "  (not delivered)");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!led.begin()) {
    Serial.println("PacketLED init failed");
    while (true) delay(1000);
  }
  Serial.println("PacketLED Chat - type a message and press Enter");
}

void loop() {
  // Collect what is typed; send it when the line is complete.
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) sendLine();
      lineLen = 0;
      lineTooLong = false;
    } else if (lineLen < sizeof(line)) {
      line[lineLen++] = c;
    } else {
      lineTooLong = true;
    }
  }

  if (led.parsePacket() > 0) {
    Serial.print("< ");
    while (led.available()) Serial.write(led.read());
    Serial.println();
  }
}
