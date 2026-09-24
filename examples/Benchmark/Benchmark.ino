/*
 * PacketLED - Benchmark
 *
 * Link test and diagnostics. Upload the same sketch to both boards and send
 * commands from the Serial Monitor (115200 baud):
 *
 *   b / B   send 10 / 100 text packets
 *   R       send 100 binary packets of 64 bytes; the receiver checks every byte
 *   i       statistics                 x   reset statistics
 *   v       per-frame diagnostics on/off (rejected frames are always printed)
 *   1 5 2   bit rate 1024 / 512 / 256 bit/s (set the same on both boards)
 *   m       raw light readings at 100 / 300 / 500 us of integration
 *   l       LED on for 3 seconds (use it with 'm' on the other board)
 *
 * Any character stops a running test.
 *
 * Wiring: LED_ANODE_PIN -> resistor -> LED anode, LED cathode -> LED_CATHODE_PIN
 * (see the README for the resistor value).
 * LED_ANODE_PIN must be an ADC1 pin.
 */

#include <PacketLED.h>

const uint8_t LED_ANODE_PIN = 32;
const uint8_t LED_CATHODE_PIN = 33;

ArduinoLedPhy phy(LED_ANODE_PIN, LED_CATHODE_PIN);
PacketLED led(phy);

bool verbose = false;
uint32_t testReceived = 0, testWrong = 0, testWrongBytes = 0;

// Test payload that the receiver can rebuild from its index:
// 'R', 'T', index (2 bytes), kind, then 0x00 / 0xFF / 0x55AA / pseudo-random bytes.
void makeTestPayload(uint16_t idx, uint8_t *out) {
  out[0] = 'R';
  out[1] = 'T';
  out[2] = (uint8_t)(idx >> 8);
  out[3] = (uint8_t)idx;
  const uint8_t kind = idx % 10;
  out[4] = kind;
  uint32_t x = 0x9E3779B9UL ^ ((uint32_t)idx * 2654435761UL);
  if (x == 0) x = 1;
  for (uint8_t i = 5; i < lx25::kMaxPayload; ++i) {
    if (kind == 0) out[i] = 0x00;
    else if (kind == 1) out[i] = 0xFF;
    else if (kind == 2) out[i] = (i & 1) ? 0xAA : 0x55;
    else {
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      out[i] = (uint8_t)x;
    }
  }
}

void printFrame(const PacketLED::FrameInfo &f) {
  const bool ok = f.result == PacketLED::Result::Ok;
  if (ok && !verbose) return;
  Serial.printf("%s %s: sync=%lums window=%luus level=%u phase=%ldus (+/-%ld) sfd@%u bits=%u "
                "margin=%ld weak=%u late=%ldus\n",
                ok ? (f.type == lx25::kTypeAck ? "ACK" : "DATA") : "REJECTED:", PacketLED::resultText(f.result),
                (unsigned long)(f.syncWidthUs / 1000), (unsigned long)f.windowUs, f.levelLsb, (long)f.phaseUs,
                (long)f.syncErrUs, f.sfdBit, f.bits, (long)(f.minMarginLsb == INT32_MAX ? 0 : f.minMarginLsb),
                f.weakBits, (long)f.lateMaxUs);
}

void printStats() {
  const PacketLED::Stats &s = led.stats();
  Serial.printf("RX: syncs=%lu (rejected %lu), frames ok=%lu, errors: signal=%lu sfd=%lu header=%lu crc=%lu; "
                "duplicates=%lu, overruns=%lu, stray ACKs=%lu\n",
                (unsigned long)s.syncs, (unsigned long)s.syncRejected, (unsigned long)s.framesOk,
                (unsigned long)s.noSignal, (unsigned long)s.noSfd, (unsigned long)s.headerErrors,
                (unsigned long)s.crcErrors, (unsigned long)s.duplicates, (unsigned long)s.rxOverruns,
                (unsigned long)s.acksIgnored);
  Serial.printf("    min level=%u, min margin=%ld\n", s.framesOk ? s.minLevelLsb : 0,
                (long)(s.framesOk ? s.minMarginLsb : 0));
  Serial.printf("TX: sent=%lu, first try=%lu, retransmissions=%lu, failed=%lu\n", (unsigned long)s.sent,
                (unsigned long)s.sentFirstTry, (unsigned long)s.retransmissions, (unsigned long)s.sendFailed);
  Serial.printf("Binary test: received=%lu, wrong content=%lu (%lu bytes)\n", (unsigned long)testReceived,
                (unsigned long)testWrong, (unsigned long)testWrongBytes);
  Serial.printf("Config: %lu bit/s, session=0x%02X, max window=%luus, dark level=%u\n",
                (unsigned long)led.bitRate(), led.session(), (unsigned long)led.maxWindowUs(), led.darkLevel());
}

// True if the user typed something (line endings and spaces are ignored).
bool userAbort() {
  while (Serial.available()) {
    const int c = Serial.peek();
    if (c == '\r' || c == '\n' || c == ' ') Serial.read();
    else return true;
  }
  return false;
}

void runBenchmark(uint16_t n, bool binary) {
  uint16_t ok = 0, firstTry = 0, done = 0;
  uint32_t bytes = 0;
  const uint32_t start = millis();
  for (uint16_t i = 0; i < n && !userAbort(); ++i) {
    uint8_t buf[lx25::kMaxPayload];
    uint8_t len;
    if (binary) {
      makeTestPayload(i, buf);
      len = lx25::kMaxPayload;
    } else {
      len = (uint8_t)snprintf((char *)buf, sizeof(buf), "Test packet %u", i);
    }
    ++done;
    const bool sent = led.send(buf, len);
    if (sent) {
      ++ok;
      bytes += len;
      if (led.lastAttempts() == 1) ++firstTry;
    }
    if (verbose || !sent || led.lastAttempts() > 1)
      Serial.printf("#%u %s (attempts %u)\n", i, sent ? "delivered" : "NOT delivered", led.lastAttempts());
  }
  const uint32_t ms = millis() - start;
  Serial.printf("%s: delivered %u/%u, first try %u, %lu ms, %lu ms/packet, throughput %lu bit/s\n",
                binary ? "BINARY TEST" : "BENCHMARK", ok, done, firstTry, (unsigned long)ms,
                (unsigned long)(done ? ms / done : 0), (unsigned long)(ms ? bytes * 8000UL / ms : 0));
}

void checkReceived(int n) {
  uint8_t data[lx25::kMaxPayload];
  led.readBytes(data, n);
  if (n == lx25::kMaxPayload && data[0] == 'R' && data[1] == 'T') {
    const uint16_t idx = (uint16_t)((data[2] << 8) | data[3]);
    uint8_t expected[lx25::kMaxPayload];
    makeTestPayload(idx, expected);
    uint8_t bad = 0;
    for (uint8_t i = 0; i < lx25::kMaxPayload; ++i) bad += data[i] != expected[i];
    ++testReceived;
    if (bad) {
      ++testWrong;
      testWrongBytes += bad;
      Serial.printf("TEST #%u: WRONG CONTENT (%u bytes)\n", idx, bad);
    } else if (verbose) {
      Serial.printf("TEST #%u kind %u: ok, level %u\n", idx, data[4], led.packetLevel());
    }
    return;
  }
  Serial.printf("< [0x%02X #%u level %u] ", led.packetSession(), led.packetSeq(), led.packetLevel());
  Serial.write(data, n);
  Serial.println();
}

void measureLight() {
  const uint32_t windows[] = {100, 300, 500};
  for (uint32_t w : windows) {
    Serial.printf("%3lu us:", (unsigned long)w);
    for (uint8_t i = 0; i < 10; ++i) {
      Serial.printf(" %4u", led.measureLight(w));
      delay(5);
    }
    Serial.println();
  }
}

void restart(uint32_t rate) {
  if (led.begin(rate)) Serial.printf("%lu bit/s (set the other board too)\n", (unsigned long)rate);
  else Serial.printf("%lu bit/s not supported\n", (unsigned long)rate);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  led.onFrame(printFrame);
  if (!led.begin()) Serial.println("PacketLED init failed");
  Serial.println("PacketLED Benchmark - commands: b B R i x v 1 5 2 m l");
  printStats();
}

void loop() {
  if (Serial.available()) {
    switch (Serial.read()) {
      case 'b': runBenchmark(10, false); break;
      case 'B': runBenchmark(100, false); break;
      case 'R': runBenchmark(100, true); break;
      case 'i': printStats(); break;
      case 'x':
        led.resetStats();
        testReceived = testWrong = testWrongBytes = 0;
        Serial.println("Statistics reset");
        break;
      case 'v':
        verbose = !verbose;
        Serial.printf("Per-frame diagnostics %s\n", verbose ? "on" : "off");
        break;
      case '1': restart(1024); break;
      case '5': restart(512); break;
      case '2': restart(256); break;
      case 'm': measureLight(); break;
      case 'l':
        led.setLed(true);
        delay(3000);
        led.setLed(false);
        break;
      default: break;
    }
  }
  const int n = led.parsePacket();
  if (n > 0) checkReceived(n);
}
