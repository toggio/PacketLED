/*
 * PacketLED v. 1.0.2 - 24/09/2026
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

#ifndef PACKETLED_LEDPHY_H
#define PACKETLED_LEDPHY_H

#include <stdint.h>

/**
 * Hardware abstraction used by PacketLED.
 *
 * The protocol only talks to the LED through this interface, so it can run on
 * other hardware or against a simulated channel (see extras/test).
 */
class LedPhy {
 public:
  virtual ~LedPhy() {}

  virtual void begin() {}

  /** Microsecond clock. Wraps around; only differences are used. */
  virtual uint32_t micros() = 0;

  /** Yields the CPU for about 1 ms. */
  virtual void idle() = 0;

  virtual void delayMs(uint32_t ms) = 0;

  virtual void ledOn() = 0;
  virtual void ledOff() = 0;

  /**
   * Uses the LED as a light sensor.
   *
   * Reverse-biases the junction, lets the photocurrent charge it for windowUs
   * and reads the voltage with the ADC.
   *
   * @param windowUs Integration time in microseconds.
   * @param startUs  Set to the time the integration actually started.
   * @return 12-bit reading (0-4095), proportional to the light received.
   */
  virtual uint16_t integrate(uint32_t windowUs, uint32_t &startUs) = 0;

  virtual uint32_t random32() = 0;
};

#ifdef ARDUINO
/**
 * LedPhy for Arduino (ESP32 family).
 *
 * Wiring: anode pin -> resistor -> LED anode, LED cathode -> cathode pin.
 * The anode pin must be an ADC1 input.
 */
class ArduinoLedPhy : public LedPhy {
 public:
  ArduinoLedPhy(uint8_t anodePin, uint8_t cathodePin);

  void begin() override;
  uint32_t micros() override;
  void idle() override;
  void delayMs(uint32_t ms) override;
  void ledOn() override;
  void ledOff() override;
  uint16_t integrate(uint32_t windowUs, uint32_t &startUs) override;
  uint32_t random32() override;

 private:
  uint8_t anode_;
  uint8_t cathode_;
};
#endif

#endif  // PACKETLED_LEDPHY_H
