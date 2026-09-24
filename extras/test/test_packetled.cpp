// PacketLED host tests, run against the simulated channel in sim_phy.h.
// Build from the library folder (or run run_tests.ps1):
//   python -m ziglang c++ -std=c++17 -O2 -I src extras/test/test_packetled.cpp src/PacketLED.cpp -o test.exe
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>

#include "../../src/PacketLED.h"
#include "sim_phy.h"

using Result = PacketLED::Result;

struct Options {
  double ppm = 50;             // each clock drifts by up to +/- ppm
  double txDelayUs = 0;        // extra time before A starts sending
  double stepAt = -1;          // true time of an ambient light step at B (-1 = none)
  double stepAmbient = 0;
};

struct Trial {
  bool delivered = false;  // B got a new packet
  bool contentOk = false;  // ... identical to the one sent
  bool ackOk = false;      // A got the matching ACK
  Result result = Result::Ok;
  PacketLED::FrameInfo info = {};
  double frameMs = 0, ackMs = 0;
};

// Swaps the two halves of one random data bit.
static void flipOneBit(Channel &ch, std::mt19937 &rng, double slot, double dataStart, int dataBits) {
  const double a = dataStart + 2 * slot * (rng() % dataBits);
  const bool firstLit = ch.litTime(a + slot * 0.3, a + slot * 0.7) > 0;
  std::vector<Channel::Ev> kept, fixed;
  for (const auto &e : ch.ev)
    if (e.t < a || e.t >= a + 2 * slot) kept.push_back(e);
  bool inserted = false;
  for (const auto &e : kept) {
    if (!inserted && e.t >= a) {
      fixed.push_back({a, !firstLit});
      fixed.push_back({a + slot, firstLit});
      inserted = true;
    }
    fixed.push_back(e);
  }
  if (!inserted) {
    fixed.push_back({a, !firstLit});
    fixed.push_back({a + slot, firstLit});
  }
  ch.ev = fixed;
}

static Trial runTrial(uint32_t bitRate, const SensorModel &m, uint32_t seed, const uint8_t *data, uint8_t len,
                      bool corrupt, const Options &o = Options()) {
  std::mt19937 rng(seed);
  Channel chA, chB;
  SimPhy pa(chA, chB, m, seed * 3 + 1), pb(chB, chA, m, seed * 3 + 2);
  std::uniform_real_distribution<double> ppm(-o.ppm, o.ppm);
  const double offA = (rng() % 4 == 0) ? 4294967296.0 - 300000.0 : (double)rng();  // sometimes close to wrap-around
  pa.setClock(0, offA, ppm(rng));
  pb.setClock(0, (double)rng(), ppm(rng));
  PacketLED a(pa), b(pb);
  Trial r;
  if (!a.begin(bitRate) || !b.begin(bitRate)) {
    printf("begin() failed at %u bit/s\n", bitRate);
    return r;
  }
  chA.clear();
  chB.clear();
  if (o.stepAt >= 0) pb.setAmbientStep(o.stepAt, o.stepAmbient);

  const double t0 = 5000 + rng() % 30000 + o.txDelayUs;  // B is already listening
  pa.setTrueNow(t0);
  pb.setTrueNow(0);
  a.beginPacket();
  a.write(data, len);
  a.endPacket(false);
  const double aEnd = pa.trueNow();
  r.frameMs = (aEnd - t0) / 1000.0;
  if (corrupt) {
    const double slot = (double)(500000 / bitRate);
    const double sync = chA.ev.front().t;
    const double dataStart = sync + lx25::kSyncUs + lx25::kGuardUs + (lx25::kPreambleBits + 8) * 2 * slot;
    flipOneBit(chA, rng, slot, dataStart, 8 * (6 + len));
  }

  while (pb.trueNow() < aEnd + 150000) {
    if (b.parsePacket() > 0 || b.packetAvailable()) {
      r.delivered = true;
      break;
    }
  }
  r.info = b.lastFrame();
  r.result = r.info.result;
  if (r.delivered) {
    uint8_t got[lx25::kMaxPayload];
    int n = 0;
    while (b.available()) got[n++] = (uint8_t)b.read();
    r.contentOk = n == len && memcmp(got, data, len) == 0 && b.packetSession() == a.session();
    // B's ACK goes back to A.
    const double bEnd = pb.trueNow();
    const uint32_t before = a.stats().framesOk;
    const double ackStart = chB.ev.empty() ? bEnd : chB.ev.front().t;
    while (pa.trueNow() < bEnd + 150000 && a.stats().framesOk == before) a.parsePacket();
    const PacketLED::FrameInfo &fa = a.lastFrame();
    r.ackOk = a.stats().framesOk > before && fa.type == lx25::kTypeAck && fa.session == a.session();
    r.ackMs = (bEnd - ackStart) / 1000.0;
  }
  return r;
}

static void makePayload(std::mt19937 &rng, uint8_t *buf, uint8_t &len) {
  len = (uint8_t)(1 + rng() % lx25::kMaxPayload);
  if (rng() % 4 == 0) len = lx25::kMaxPayload;
  const int kind = rng() % 5;
  for (uint8_t i = 0; i < len; ++i)
    buf[i] = kind == 0 ? 0x00 : kind == 1 ? 0xFF : kind == 2 ? (i & 1 ? 0xAA : 0x55) : (uint8_t)rng();
}

struct Case {
  const char *name;
  double gain, ambient, hum;
  bool required;  // false = extreme case, reported only
  Options opt;
};

static bool expectedToWork(const Case &c, uint32_t rate) {
  if (!c.required) return false;
  if (rate == 1024) return c.gain >= 1.1;
  return rate == 512 ? c.gain >= 0.35 : c.gain >= 0.2;
}

int main() {
  int failures = 0;
  Options drift;
  drift.ppm = 100;
  Options lampOn;
  lampOn.txDelayUs = 300000;
  lampOn.stepAt = 2000;
  lampOn.stepAmbient = 2.0;
  Options lampOff = lampOn;
  lampOff.stepAmbient = -2.0;
  const Case cases[] = {
      {"2 cm", 4.5, 0, 0, true, {}},
      {"3 cm", 1.1, 0, 0, true, {}},
      {"5 cm", 0.5, 0, 0, true, {}},
      {"6 cm", 0.35, 0, 0, true, {}},
      {"8 cm", 0.2, 0, 0, true, {}},
      {"3 cm, +/-100 ppm clocks", 1.1, 0, 0, true, drift},
      {"3 cm, ambient light", 1.1, 2.0, 0, true, {}},
      {"3 cm, lamp switched on", 1.1, 0, 0, true, lampOn},
      {"3 cm, lamp switched off", 1.1, 2.0, 0, true, lampOff},
      // measured mains flicker: ~0.03-0.08 LSB/us in the dark; 0.2 is already pessimistic
      {"3 cm, 50 Hz flicker", 1.1, 0, 0.2, true, {}},
      {"5 cm, 50 Hz flicker", 0.5, 0, 0.2, true, {}},
      {"5 cm, extreme flicker", 0.5, 0, 0.6, false, {}},
  };
  printf("== Delivery (payload 1-64 bytes: 0x00/0xFF/0x55AA/random), independent clocks ==\n");
  for (uint32_t rate : {1024u, 512u, 256u}) {
    for (const Case &c : cases) {
      SensorModel m;
      m.gain = c.gain;
      m.ambient = c.ambient;
      m.hum = c.hum;
      std::mt19937 rng((uint32_t)(c.gain * 1000 + c.ambient * 77 + c.hum * 55 + c.opt.ppm + c.opt.stepAmbient * 13 +
                                  rate));
      const int n = 100;
      int ok = 0, acks = 0, wrong = 0;
      int32_t minMargin = INT32_MAX;
      uint32_t window = 0, level = 0xFFFF;
      std::map<std::string, int> fails;
      for (int i = 0; i < n; ++i) {
        uint8_t buf[lx25::kMaxPayload], len;
        makePayload(rng, buf, len);
        const Trial t = runTrial(rate, m, rng(), buf, len, false, c.opt);
        if (t.delivered && !t.contentOk) ++wrong;
        if (t.contentOk) ++ok;
        else ++fails[PacketLED::resultText(t.result)];
        if (t.ackOk) ++acks;
        if (t.contentOk) {
          if (t.info.minMarginLsb < minMargin) minMargin = t.info.minMarginLsb;
          if (t.info.levelLsb < level) level = t.info.levelLsb;
          window = t.info.windowUs;
        }
      }
      const bool expected = expectedToWork(c, rate);
      printf("%4u bit/s %-26s %s delivered %3d/%d, ACK %3d/%d, wrong %d, window %4u us, min level %4u, "
             "min margin %4d",
             rate, c.name, expected ? "*" : " ", ok, n, acks, n, wrong, window, level == 0xFFFF ? 0 : level,
             minMargin == INT32_MAX ? -1 : minMargin);
      for (auto &f : fails) printf(" | %s: %d", f.first.c_str(), f.second);
      printf("\n");
      if (wrong) ++failures;
      if (expected && (ok != n || acks != n)) ++failures;
    }
  }
  printf("(* = must deliver 100%%)\n");

  printf("\n== One data bit flipped (never deliver wrong content) ==\n");
  for (uint32_t rate : {1024u, 512u}) {
    SensorModel m;
    std::mt19937 rng(7 + rate);
    int delivered = 0, wrong = 0;
    std::map<std::string, int> fails;
    for (int i = 0; i < 200; ++i) {
      uint8_t buf[lx25::kMaxPayload], len;
      makePayload(rng, buf, len);
      const Trial t = runTrial(rate, m, rng(), buf, len, true);
      if (t.delivered) ++delivered;
      if (t.delivered && !t.contentOk) ++wrong;
      if (!t.delivered) ++fails[PacketLED::resultText(t.result)];
    }
    printf("%4u bit/s: delivered %d/200 (wrong content %d)", rate, delivered, wrong);
    for (auto &f : fails) printf(" | %s: %d", f.first.c_str(), f.second);
    printf("\n");
    if (wrong || delivered) ++failures;
  }

  printf("\n== API ==\n");
  {
    Channel chA, chB;
    SensorModel m;
    SimPhy pa(chA, chB, m, 1);
    PacketLED a(pa);
    bool ok = a.begin(1024) && !a.begin(255) && !a.begin(1025) && a.begin(1024);
    chA.clear();
    uint8_t big[lx25::kMaxPayload + 1] = {};
    a.beginPacket();
    ok = ok && a.write(big, sizeof(big)) == lx25::kMaxPayload;
    ok = ok && !a.endPacket(false);  // overflow: not sent
    ok = ok && !a.send(big, sizeof(big));
    ok = ok && chA.ev.empty();
    printf("begin() range, write() overflow, send() too long: %s\n", ok ? "OK" : "FAILED");
    if (!ok) ++failures;
  }

  printf("\n== Frame duration ==\n");
  for (uint32_t rate : {1024u, 512u, 256u}) {
    SensorModel m;
    uint8_t buf[lx25::kMaxPayload] = {1};
    const Trial t16 = runTrial(rate, m, 6, buf, 16, false);
    const Trial t32 = runTrial(rate, m, 7, buf, 32, false);
    const Trial t64 = runTrial(rate, m, 8, buf, 64, false);
    printf("%4u bit/s: 16 bytes %6.1f ms, 32 bytes %6.1f ms, 64 bytes %6.1f ms, ACK %5.1f ms\n", rate, t16.frameMs,
           t32.frameMs, t64.frameMs, t64.ackMs);
  }

  printf("\n%s\n", failures ? "*** SOME TESTS FAILED ***" : "ALL TESTS PASSED");
  return failures ? 1 : 0;
}
