# PacketLED API

Reference for PacketLED 1.0.1. See the [README](README.md) for wiring and an overview.

## Setup

```cpp
#include <PacketLED.h>

ArduinoLedPhy phy(anodePin, cathodePin);
PacketLED led(phy);
```

### `ArduinoLedPhy(uint8_t anodePin, uint8_t cathodePin)`

The physical layer for ESP32 boards. `anodePin` goes to the LED anode through the resistor and must be an ADC1 input. `cathodePin` goes to the LED cathode. Declare it as a global and pass it to `PacketLED`.

### `bool begin(uint32_t bitRate = 1024)`

Initializes the pins and the ADC and measures the dark level. Call it while the LED of the other board is off.

`bitRate` must be the same on both boards. Use 1024 (about 3 cm), 512 (about 5 cm) or 256 (about 8 cm). Any value in between works too.

Returns `false` if the bit rate is out of range or too fast for the board; the previous settings are then kept. It can be called again at any time to change the bit rate.

### `void end()`

Turns the LED off.

## Sending

### `int beginPacket()`

Starts a new packet. Always returns 1.

### `size_t write(uint8_t b)`
### `size_t write(const uint8_t *buffer, size_t size)`

Adds bytes to the packet. `print()` and `println()` work too. A packet holds up to 64 bytes (`lx25::kMaxPayload`). Bytes beyond that are not added: `write()` returns 0 and `endPacket()` fails.

### `bool endPacket(bool confirmed = true)`

Sends the packet.

- `confirmed = true`: waits up to 400 ms for the acknowledgement and retransmits after a short random pause, up to 3 attempts in total. Returns `true` when the packet has been acknowledged.
- `confirmed = false`: sends once and returns `true`. The receiver still acknowledges it, but the acknowledgement is ignored.

Returns `false` if the packet was longer than 64 bytes. The call blocks until the outcome is known.

```cpp
led.beginPacket();
led.print("T=");
led.print(23.4);
if (!led.endPacket()) Serial.println("not delivered");
```

### `bool send(const uint8_t *data, size_t len)`
### `bool send(const char *text)`

Shortcuts for `beginPacket()`, `write()` and `endPacket(true)`.

### `uint8_t lastAttempts()`

Attempts used by the last confirmed send: 1 means first try, 0 means not delivered.

## Receiving

### `int parsePacket()`

Listens for a packet. Call it continuously from `loop()`.

- Returns the size of a new packet, or 0.
- New packets are acknowledged automatically.
- Duplicates are acknowledged again but not returned.
- When nothing arrives it returns in about 2 ms. While a frame arrives it blocks for the length of the frame, plus the acknowledgement.

A packet received while `endPacket()` was waiting for its own acknowledgement is returned by the next `parsePacket()`. If a second packet arrives before that call, it is not acknowledged, so the sender retransmits it later. `Stats::rxOverruns` counts these cases.

### `bool packetAvailable()`

`true` if the last `parsePacket()` returned a packet. Only needed to detect empty packets.

### `int available()`, `int read()`, `int peek()`

Reads the received packet byte by byte, as with `Serial`. `readBytes()` also works.

```cpp
int size = led.parsePacket();
if (size > 0) {
  uint8_t data[lx25::kMaxPayload];
  led.readBytes(data, size);
}
```

### Information about the last packet

| Method | Returns |
|---|---|
| `uint8_t packetSession()` | Session of the sender (random, changes when it restarts) |
| `uint8_t packetSeq()` | Sequence number of the packet |
| `uint16_t packetLevel()` | Signal amplitude in ADC units. Higher means more light, like RSSI on a radio |
| `int32_t packetMargin()` | Smallest difference between the two halves of a bit. The margin against errors, like SNR |

## Diagnostics

### `void onFrame(void (*handler)(const FrameInfo &))`

Registers a function called after every received frame: data, acknowledgements and rejected frames. It is called once the frame has been fully measured, so it can print freely. Do not call `endPacket()`, `send()` or `parsePacket()` from it.

### `const FrameInfo &lastFrame()`, `const Stats &stats()`, `void resetStats()`

Details of the last frame and cumulative counters.

### `static const char *resultText(Result r)`

A short description of a `Result`.

### Other information

| Method | Returns |
|---|---|
| `uint8_t session()` | Session of this board |
| `uint32_t bitRate()` | Bit rate set by `begin()` |
| `uint32_t maxWindowUs()` | Longest integration time at this bit rate |
| `uint16_t darkLevel()` | Current dark level while listening |
| `uint16_t noiseLevel()` | Average deviation of the dark readings, mostly mains hum. The SYNC threshold is kept above it |
| `uint16_t measureLight(uint32_t windowUs)` | One raw reading (0-4095) with the given integration time |
| `void setLed(bool on)` | Turns the LED on or off, for testing. An LED left on is seen by the other board as ambient light |

## Types

### `PacketLED::Result`

| Value | Meaning | Usual cause |
|---|---|---|
| `Ok` | Valid frame | |
| `NoSignal` | Preamble missing or too weak | LEDs too far apart or misaligned |
| `NoSfd` | Preamble found, no start delimiter | Interference during the preamble |
| `BadHeader` | Invalid type or length | Bit errors in the header |
| `BadCrc` | Frame check sequence mismatch | Bit errors in the data |

### `PacketLED::FrameInfo`

Filled for every received frame. Fields after the point of failure are 0.

| Field | Meaning |
|---|---|
| `result` | Outcome |
| `type`, `session`, `seq`, `len` | Header |
| `syncWidthUs` | SYNC length seen by the listener (nominally 20000) |
| `windowUs` | Integration time used for the frame |
| `syncErrUs` | Uncertainty on the end of the SYNC |
| `phaseUs` | Bit phase measured on the preamble |
| `levelLsb` | Signal amplitude |
| `sfdBit` | Bits read before finding the start delimiter |
| `bits` | Data bits read |
| `weakBits` | Bits with less than a quarter of the signal amplitude |
| `minMarginLsb` | Smallest difference between the two halves of a bit |
| `lateMaxUs` | Worst delay in starting a measurement; should stay within a few µs |

### `PacketLED::Stats`

| Field | Meaning |
|---|---|
| `syncs`, `syncRejected` | SYNCs detected, and light pulses of the wrong length |
| `framesOk` | Valid frames (data and acknowledgements) |
| `noSignal`, `noSfd`, `headerErrors`, `crcErrors` | Rejected frames by cause |
| `duplicates` | Packets received again and acknowledged again |
| `rxOverruns` | Packets not accepted because the previous one had not been collected yet |
| `acksIgnored` | Acknowledgements for another session or sequence number |
| `sent`, `sentFirstTry`, `retransmissions`, `sendFailed` | Confirmed sends |
| `minMarginLsb`, `minLevelLsb` | Lowest margin and level over valid frames |

Until the first valid frame, `minMarginLsb` is `INT32_MAX` and `minLevelLsb` is 65535.

## Constants

The `lx25` namespace holds the LX.25 protocol constants, such as `lx25::kMaxPayload` (64). If you change one, change it on both boards, or they will not understand each other.

`PACKETLED_VERSION` holds the library version as a string.

## Porting: `LedPhy`

`PacketLED` never touches pins directly. It uses a `LedPhy` object, so the protocol can run on other hardware, or on a PC against the simulator in `extras/test`.

```cpp
class LedPhy {
 public:
  virtual void begin() {}
  virtual uint32_t micros() = 0;       // microsecond clock, may wrap around
  virtual void idle() = 0;             // yield for about 1 ms
  virtual void delayMs(uint32_t ms) = 0;
  virtual void ledOn() = 0;
  virtual void ledOff() = 0;
  virtual uint16_t integrate(uint32_t windowUs, uint32_t &startUs) = 0;
  virtual uint32_t random32() = 0;
};
```

`integrate()` reverse-biases the LED, lets it integrate the light for `windowUs` and returns a 12-bit reading proportional to the light. `startUs` receives the time the integration started, to the microsecond. The fixed costs (pin setup, ADC conversion) may be anything, as long as they are repeatable: `begin()` measures them.
