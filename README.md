# PacketLED

Packet communication over bidirectional LEDs, inspired by Packet Radio.

## Overview

PacketLED lets two boards exchange data through a pair of ordinary LEDs placed face to face. There is no photodiode: each LED emits light to transmit, and is used as a light sensor to receive. One LED and one resistor per board are all you need.

## Features

- **Two-way link with one part per side.** The same LED sends and receives, so a device can talk through the status LED it already has.
- **Short range by design.** The link works over a few centimeters, with the LEDs facing each other. To pick up the data you have to be right in the light path. There are no radio emissions, no pairing, no network stack and nothing listening from across the room, which keeps the attack surface small. The data is not encrypted: if it is secret, encrypt it before sending.
- **Reliable delivery.** Every packet is checked, acknowledged and retransmitted if needed, and duplicates are discarded. `endPacket()` tells you whether the other side got it.
- **No wires between the boards.** The two devices share only light: no common ground, no connectors, full electrical isolation.
- **No radio at all.** Useful where RF is unwanted or not allowed, and unaffected by radio interference.
- **Copes with room light.** Ambient light, lamps being switched on and off and mains flicker are handled automatically.
- **Familiar API.** If you have used the Arduino LoRa library, or `Serial`, you already know how to use it.

Some things it is good for:

- configuring a device by holding another one against it: WiFi credentials, keys, settings;
- reading data out of a device without opening it or adding a connector;
- a service or debug port through an existing indicator LED;
- learning and experimenting with optical communication.

## Protocol

Data travels in small packets, as in amateur Packet Radio. The link protocol is called **LX.25**, after AX.25, and provides:

- packets of 0 to 64 bytes, text or binary;
- a CRC-16/X.25 checksum on every frame (the same FCS as AX.25);
- acknowledgements, up to 3 retransmissions, and duplicate filtering.

The speed is 256, 512 or 1024 bit/s, chosen in `begin()`. The full specification, detailed enough to write a compatible implementation, is in [LX25.md](LX25.md).

## Requirements

- An ESP32 board. Tested on ESP32 and ESP32-C3 with the Arduino-ESP32 core 3.0.7.
- One LED and one resistor per board. Use the same kind of LED on both sides; standard red LEDs work well.

## Wiring

```
anode pin (ADC1) ── R ── LED anode
cathode pin ──────────── LED cathode
```

The anode pin must be an ADC1 input: ADC2 cannot be used while WiFi is on. The cathode pin can be any GPIO. Point the two LEDs at each other.

Keep the wires to the LED short. While receiving, the anode is left floating, and long wires pick up mains hum.

### Choosing the resistor

The resistor sets the LED current while transmitting. More current means more light and more range. Aim for 5 to 20 mA, within the LED rating and the pin limit (on the ESP32, 20 mA per pin is a sensible maximum):

```
R = (3.3 V - Vf) / I
```

`Vf` is the LED forward voltage, about 1.8-2.1 V for red. For a red LED this gives roughly 270 Ω at 5 mA and 68 Ω at 20 mA. Blue and white LEDs have a Vf close to 3 V, which leaves very little headroom at 3.3 V.

The resistor plays no part in receiving. The ranges below were found with 470 Ω (about 3 mA), so a lower value will do better.

## Installation

In the Arduino IDE, open the Library Manager, search for **PacketLED** and click *Install*.

You can also download the repository as a ZIP file and add it with *Sketch > Include Library > Add .ZIP Library*.

## Usage

The API follows the Arduino LoRa library (`beginPacket()`, `endPacket()`, `parsePacket()`), and `PacketLED` is a `Stream`, so `print()` and `readBytes()` work as they do on `Serial`. The complete reference is in [API.md](API.md).

```cpp
#include <PacketLED.h>

ArduinoLedPhy phy(32, 33);   // anode pin, cathode pin
PacketLED led(phy);

void setup() {
  Serial.begin(115200);
  led.begin();               // 1024 bit/s, the same on both boards
}

void loop() {
  // Send, and wait for the other board to acknowledge
  led.beginPacket();
  led.print("Hello");
  if (!led.endPacket()) Serial.println("not delivered");

  // Receive
  int size = led.parsePacket();
  if (size > 0) {
    while (led.available()) Serial.write(led.read());
    Serial.println();
  }
}
```

A few things work differently from a radio library.

**Call `parsePacket()` often.** No radio chip listens in the background: the board only hears the other side while `parsePacket()` or `endPacket()` is running. The sender retransmits a missed packet, but a long `delay()` in `loop()` means more retransmissions.

**Calls block.** Sending or receiving a frame takes as long as the frame itself (see the table below).

**`endPacket()` returns `true` only when the packet has been delivered**, that is, acknowledged by the other board. `endPacket(false)` sends once without waiting.

**Call `begin()` with the other LED off.** It measures the dark level. After that, the dark level follows the ambient light by itself, including a lamp being switched on or off.

## Speed and range

| Bit rate | Range | 16 bytes | 64 bytes | ACK |
|---|---|---|---|---|
| `begin()`, 1024 bit/s | about 3 cm | 218 ms | 593 ms | 93 ms |
| `begin(512)` | about 5 cm | 414 ms | 1163 ms | 164 ms |
| `begin(256)` | about 8 cm | 804 ms | 2304 ms | 304 ms |

The ranges are indicative. They were found with generic 3 mm red LEDs at 470 Ω, partly on hardware and partly with the simulator in `extras/test`, which is calibrated on hardware measurements. On the bench, at 1 cm and full speed, 100 binary packets went through in each direction, all at the first attempt.

Ways to get more range:

- a lower resistor (see above);
- high-brightness, clear, narrow-angle LEDs;
- a short black tube around each LED to keep stray light out.

## How it works

To receive, the LED is reverse-biased and then its anode is left floating. Light falling on the junction produces a tiny current that charges it. After a fixed integration time the ADC reads the voltage, which is proportional to the light received. This is the technique described by Dietz, Yerazunis and Leigh at MERL (see [Credits](#credits)). Where the original work timed a digital input, PacketLED reads the voltage with the ADC, which is far more sensitive at a distance.

An LX.25 frame looks like this (see [LX25.md](LX25.md) for the details):

| Field | Length | Purpose |
|---|---|---|
| SYNC | 20 ms | steady light; the receiver picks its integration time from it |
| GUARD | 3 ms | dark |
| PREAMBLE | 16 bits | the receiver locks onto the bit phase |
| SFD | 1 byte | 0xA7, start of the data |
| HEADER | 4 bytes | type, session, sequence number, length |
| PAYLOAD | 0-64 bytes | |
| FCS | 2 bytes | CRC-16/X.25 |

Bits are Manchester coded: `0` is light then dark, `1` is dark then light. The receiver compares the two halves of each bit, so there is no threshold to calibrate. Steady ambient light and slow flicker, such as 50 Hz mains lighting, cancel out.

Each board picks a random session number when it starts. Together with the sequence number, it lets the receiver discard duplicates, even after the sender reboots.

## Examples

- **BasicSend** and **BasicReceive**: one board sends text, raw bytes, a struct and an unconfirmed packet, and the other prints what it gets.
- **Chat**: a two-way text chat. Upload it to both boards and type in the Serial Monitor.
- **SyncBlink**: the two boards blink a Morse message in step, on the same LEDs they use to talk. A short packet marks a common starting point; no clock sync code is needed.
- **Benchmark**: throughput and error tests, per-frame diagnostics, statistics and raw light readings. Upload it to both boards and type commands in the Serial Monitor.

## Tests

`extras/test` runs the library on a PC against a simulated optical channel built from measurements of real hardware. The simulation includes:

- ADC noise;
- ambient light, and lamps switched on and off;
- mains flicker;
- independent, drifting clocks on the two boards.

```bash
python -m pip install ziglang
python -m ziglang c++ -std=c++17 -O2 -I src extras/test/test_packetled.cpp src/PacketLED.cpp -o test.exe
```

On Windows, `extras/test/run_tests.ps1` builds and runs it in one step.

## Limitations

- ESP32 only for now. Only the small physical layer depends on the ESP32 (see `LedPhy` in [API.md](API.md)); the protocol itself is portable.
- Point to point: there are no addresses.
- Timing is done by busy waiting, so heavy interrupt load can disturb it. `FrameInfo::lateMaxUs` shows how late the measurements start. The library has not been tested with WiFi active.
- At 256 bit/s a 64-byte frame keeps the CPU busy for over 2 seconds.

## Credits

- P. Dietz, W. Yerazunis, D. Leigh, [*Very Low-Cost Sensing and Communication Using Bidirectional LEDs*](https://www.merl.com/publications/docs/TR2003-35.pdf), MERL TR2003-35, 2003. The original idea of using the same LED to transmit and to sense light.
- [AX.25](https://www.ax25.net/AX25.2.2-Jul%2098-2.pdf) and amateur Packet Radio, for the name, the idea of a small acknowledged packet link, and the frame check sequence.
- Sandeep Mistry's [arduino-LoRa](https://github.com/sandeepmistry/arduino-LoRa) library, whose API PacketLED follows.

## Help us

If you find this project useful and would like to support its development, consider making a donation. Any contribution is greatly appreciated!

**Bitcoin (BTC) Addresses:**
- **1LToggio**f3rNUTCemJZSsxd1qubTYoSde6
- **3LToggio**7Xx8qMsjCFfiarV4U2ZR9iU9ob

## License

**PacketLED** library is licensed under the Apache License, Version 2.0. You are free to use, modify, and distribute the library in compliance with the license.

Copyright (C) 2026 Luca Soltoggio - https://www.lucasoltoggio.it/
