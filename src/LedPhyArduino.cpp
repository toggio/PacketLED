/*
 * PacketLED v. 1.0.0 - 24/09/2026
 *
 * Packet communication over bidirectional LEDs, inspired by Packet Radio.
 *
 * Copyright (C) 2026 under Apache License, Version 2.0
 *
 * @author Luca Soltoggio
 * https://www.lucasoltoggio.it
 * https://github.com/toggio/PacketLED
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifdef ARDUINO

#include <Arduino.h>

#include "LedPhy.h"

#ifndef ARDUINO_ARCH_ESP32
#error "PacketLED currently supports ESP32 boards only."
#endif

ArduinoLedPhy::ArduinoLedPhy(uint8_t anodePin, uint8_t cathodePin)
    : anode_(anodePin), cathode_(cathodePin) {}

void ArduinoLedPhy::begin() {
  analogReadResolution(12);
  // 0 dB attenuation (about 0-1 V full scale): the photocurrent only charges the
  // junction by a few hundred mV. Set as the default too, because pinMode()
  // changes re-attach the pin to the ADC.
  analogSetAttenuation(ADC_0db);
  analogSetPinAttenuation(anode_, ADC_0db);
  ledOff();
}

uint32_t ArduinoLedPhy::micros() { return ::micros(); }

void ArduinoLedPhy::idle() { ::delay(1); }

void ArduinoLedPhy::delayMs(uint32_t ms) { ::delay(ms); }

void ArduinoLedPhy::ledOff() {
  pinMode(anode_, OUTPUT);
  digitalWrite(anode_, LOW);
  pinMode(cathode_, OUTPUT);
  digitalWrite(cathode_, LOW);
}

void ArduinoLedPhy::ledOn() {
  pinMode(cathode_, OUTPUT);
  digitalWrite(cathode_, LOW);
  pinMode(anode_, OUTPUT);
  digitalWrite(anode_, HIGH);
}

uint16_t ArduinoLedPhy::integrate(uint32_t windowUs, uint32_t &startUs) {
  // Reverse bias: cathode high, anode discharged to 0 V, then left floating.
  pinMode(cathode_, OUTPUT);
  digitalWrite(cathode_, HIGH);
  pinMode(anode_, OUTPUT);
  digitalWrite(anode_, LOW);
  delayMicroseconds(20);
  pinMode(anode_, INPUT);
  const uint32_t start = ::micros();
  while ((uint32_t)(::micros() - start) < windowUs) {
  }
  const uint16_t v = analogRead(anode_);
  ledOff();
  startUs = start;
  return v;
}

uint32_t ArduinoLedPhy::random32() { return esp_random(); }

#endif  // ARDUINO
