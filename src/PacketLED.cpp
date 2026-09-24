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

#include "PacketLED.h"

#include <math.h>
#include <string.h>

using namespace lx25;

namespace {

constexpr uint8_t kHeaderLen = 4;
constexpr uint8_t kFcsLen = 2;
const float kTwoPi = 6.2831853f;

// CRC-16/X.25, the AX.25 frame check sequence: reflected poly 0x1021 (0x8408),
// init 0xFFFF, final XOR 0xFFFF.
uint16_t fcs16(const uint8_t *buf, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; ++b) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
  }
  return (uint16_t)~crc;
}

template <typename T>
void sortSmall(T *a, uint8_t n) {
  for (uint8_t i = 1; i < n; ++i)
    for (uint8_t j = i; j > 0 && a[j] < a[j - 1]; --j) {
      const T t = a[j];
      a[j] = a[j - 1];
      a[j - 1] = t;
    }
}

}  // namespace

PacketLED::PacketLED(LedPhy &phy) : phy_(phy) { resetStats(); }

void PacketLED::resetStats() {
  stats_ = Stats{};
  stats_.minMarginLsb = INT32_MAX;
  stats_.minLevelLsb = 0xFFFF;
}

// ---------------------------------------------------------------- setup

bool PacketLED::begin(uint32_t bitRate) {
  if (bitRate < kMinBitRate || bitRate > kMaxBitRate) return false;
  const uint32_t slotUs = 500000 / bitRate;  // half a bit
  phy_.begin();
  phy_.ledOff();

  // Fixed cost of a measurement (pin setup, ADC read): median of 15.
  uint32_t setup[15], cycle[15];
  for (uint8_t i = 0; i < 15; ++i) {
    uint32_t st = 0;
    const uint32_t t0 = phy_.micros();
    (void)phy_.integrate(0, st);
    cycle[i] = phy_.micros() - t0;
    setup[i] = st - t0;
    phy_.idle();
  }
  sortSmall(setup, 15);
  sortSmall(cycle, 15);
  // A full measurement must fit in half a bit, with some margin. On failure the
  // previous settings are left untouched.
  if (slotUs < cycle[7] + 40 + kMinWindowUs) return false;
  bitRate_ = bitRate;
  slotUs_ = slotUs;
  setupUs_ = setup[7];
  maxWindowUs_ = slotUs_ - cycle[7] - 40;
  if (maxWindowUs_ > slotUs_ / 2) maxWindowUs_ = slotUs_ / 2;

  // At low rates the data window gets longer: listen at least as long, so that a
  // SYNC is seen wherever the data can be read.
  listenUs_ = maxWindowUs_ > kListenWindowUs ? maxWindowUs_ : kListenWindowUs;
  if (listenUs_ > kMaxListenWindowUs) listenUs_ = kMaxListenWindowUs;

  uint16_t d[15];
  for (uint8_t i = 0; i < 15; ++i) {
    uint32_t st = 0;
    d[i] = phy_.integrate(listenUs_, st);
    phy_.idle();
  }
  // Session and first sequence number: the hardware RNG mixed with timing and
  // ADC noise, so that a restarted sender is unlikely to be taken for a duplicate.
  uint32_t seed = phy_.random32();
  for (uint8_t i = 0; i < 15; ++i) seed = (seed << 5 | seed >> 27) ^ cycle[i] ^ ((uint32_t)d[i] << 11);
  session_ = (uint8_t)(seed ^ seed >> 8 ^ seed >> 16 ^ seed >> 24);
  nextSeq_ = (uint8_t)(phy_.random32() ^ seed >> 13);

  sortSmall(d, 15);
  uint32_t dev = 0;
  for (uint8_t i = 0; i < 15; ++i) dev += d[i] > d[7] ? d[i] - d[7] : d[7] - d[i];
  noiseLsb_ = (uint16_t)(dev / 15);
  setDark(d[7]);
  prevLight_ = false;
  resumed_ = false;
  lastListenUs_ = phy_.micros();
  txLen_ = 0;
  txOverflow_ = false;
  return true;
}

void PacketLED::end() { phy_.ledOff(); }

uint16_t PacketLED::measureLight(uint32_t windowUs) {
  uint32_t st = 0;
  return phy_.integrate(windowUs, st);
}

void PacketLED::setLed(bool on) {
  if (on) phy_.ledOn();
  else phy_.ledOff();
}

// The SYNC threshold sits above the dark level by the largest of a fixed
// minimum, a quarter of the dark level and a multiple of the resting noise, so
// that mains hum picked up by a sensitive LED is not taken for light.
void PacketLED::setDark(uint16_t dark) {
  darkLsb_ = dark;
  uint32_t margin = kMinLightLsb;
  if (dark / 4 > margin) margin = dark / 4;
  if ((uint32_t)noiseLsb_ * kNoiseFactor > margin) margin = (uint32_t)noiseLsb_ * kNoiseFactor;
  lightThrLsb_ = (uint16_t)(dark + margin > 0xFFFF ? 0xFFFF : dark + margin);
}

// ---------------------------------------------------------------- timing

void PacketLED::waitUntil(uint32_t t) {
  while ((int32_t)(phy_.micros() - t) < 0) {
  }
}

// Integration that starts exactly at startAt, with the window chosen for the frame.
uint16_t PacketLED::integrateAt(uint32_t startAt, uint32_t &startUs) {
  waitUntil(startAt - setupUs_);
  const uint16_t v = phy_.integrate(frameWindowUs_, startUs);
  int32_t late = (int32_t)(startUs - startAt);
  if (late < 0) late = -late;
  if (late > frame_.lateMaxUs) frame_.lateMaxUs = late;
  return v;
}

// ---------------------------------------------------------------- transmit

int PacketLED::beginPacket() {
  txLen_ = 0;
  txOverflow_ = false;
  return 1;
}

size_t PacketLED::write(uint8_t b) {
  if (txLen_ >= kMaxPayload) {
    txOverflow_ = true;
    return 0;
  }
  txBuf_[txLen_++] = b;
  return 1;
}

size_t PacketLED::write(const uint8_t *buf, size_t n) {
  size_t k = 0;
  while (k < n && write(buf[k])) ++k;
  return k;
}

bool PacketLED::endPacket(bool confirmed) {
  bool ok = false;
  if (txOverflow_) lastAttempts_ = 0;
  else ok = transmitPacket(confirmed);
  txLen_ = 0;
  txOverflow_ = false;
  return ok;
}

bool PacketLED::send(const uint8_t *data, size_t len) {
  beginPacket();
  if (len > kMaxPayload) {
    lastAttempts_ = 0;
    return false;
  }
  write(data, len);
  return endPacket(true);
}

bool PacketLED::send(const char *text) { return send((const uint8_t *)text, strlen(text)); }

bool PacketLED::transmitPacket(bool confirmed) {
  const uint8_t seq = nextSeq_++;
  if (!confirmed) {
    transmit(kTypeData, session_, seq, txBuf_, txLen_);
    return true;
  }
  ++stats_.sent;
  waitingAck_ = true;
  wantedSeq_ = seq;
  lastAttempts_ = 0;
  for (uint8_t attempt = 1; attempt <= kMaxRetries; ++attempt) {
    ackMatched_ = false;
    transmit(kTypeData, session_, seq, txBuf_, txLen_);
    const uint32_t t0 = phy_.micros();
    while ((uint32_t)(phy_.micros() - t0) < kAckTimeoutMs * 1000UL) {
      listenOnce();
      if (ackMatched_) {
        waitingAck_ = false;
        lastAttempts_ = attempt;
        if (attempt == 1) ++stats_.sentFirstTry;
        stats_.retransmissions += attempt - 1;
        return true;
      }
    }
    if (attempt < kMaxRetries) phy_.delayMs(20 + phy_.random32() % 60);  // random backoff
  }
  waitingAck_ = false;
  stats_.retransmissions += kMaxRetries - 1;
  ++stats_.sendFailed;
  return false;
}

void PacketLED::transmit(uint8_t type, uint8_t session, uint8_t seq, const uint8_t *data, uint8_t len) {
  uint8_t buf[kHeaderLen + kMaxPayload + kFcsLen];
  buf[0] = type;
  buf[1] = session;
  buf[2] = seq;
  buf[3] = len;
  if (len) memcpy(buf + kHeaderLen, data, len);
  const uint16_t fcs = fcs16(buf, kHeaderLen + len);
  buf[kHeaderLen + len] = (uint8_t)fcs;
  buf[kHeaderLen + len + 1] = (uint8_t)(fcs >> 8);

  // Give the other side time to get back to listening after its last frame.
  const uint32_t since = phy_.micros() - lastRxEndUs_;
  if (since < kTxGapMs * 1000UL) phy_.delayMs(kTxGapMs - since / 1000);

  const uint32_t t0 = phy_.micros() + 100;
  waitUntil(t0);
  phy_.ledOn();
  waitUntil(t0 + kSyncUs);
  phy_.ledOff();

  uint32_t t = t0 + kSyncUs + kGuardUs;
  auto sendBit = [&](bool one) {
    waitUntil(t);
    if (one) phy_.ledOff();
    else phy_.ledOn();
    waitUntil(t + slotUs_);
    if (one) phy_.ledOn();
    else phy_.ledOff();
    t += 2 * slotUs_;
  };
  auto sendByte = [&](uint8_t v) {
    for (int8_t b = 7; b >= 0; --b) sendBit((v >> b) & 1);
  };
  for (uint8_t i = 0; i < kPreambleBits; ++i) sendBit(false);
  sendByte(kSfd);
  for (uint8_t i = 0; i < kHeaderLen + len + kFcsLen; ++i) sendByte(buf[i]);
  waitUntil(t);
  phy_.ledOff();
  prevLight_ = false;
  resumed_ = true;
}

// ---------------------------------------------------------------- receive

int PacketLED::parsePacket() {
  if (!pending_) listenOnce();
  rxReady_ = pending_;
  if (!pending_) return 0;
  pending_ = false;
  rxPos_ = 0;
  return rxLen_;
}

int PacketLED::available() { return rxLen_ - rxPos_; }

int PacketLED::read() { return rxPos_ < rxLen_ ? rxBuf_[rxPos_++] : -1; }

int PacketLED::peek() { return rxPos_ < rxLen_ ? rxBuf_[rxPos_] : -1; }

// One listening measurement. A SYNC is a run of "light" readings lasting
// kSyncMinUs..kSyncMaxUs; the frame is received from the first dark reading.
PacketLED::Event PacketLED::listenOnce() {
  uint32_t st = 0;
  const uint16_t v = phy_.integrate(listenUs_, st);
  // After a pause in listening (the board was sending, or busy between two
  // calls) a light pulse may have started unseen, and its length is unknown.
  const bool away = resumed_ || (uint32_t)(st - lastListenUs_) > listenUs_ + kListenGapUs;
  resumed_ = false;
  lastListenUs_ = st;
  // If listening stopped while a pulse was on, its end is unknown too: start over.
  if (away) prevLight_ = false;
  // The end of the SYNC is detected halfway between dark and SYNC level, so that
  // ambient light and mains flicker do not delay it.
  uint16_t thr = lightThrLsb_;
  if (prevLight_) {
    const uint16_t mid = (uint16_t)(((uint32_t)darkLsb_ + syncLsb_) / 2);
    if (mid > thr) thr = mid;
  }
  if (v > thr) {
    if (!prevLight_) {
      riseStart_ = st;
      riseSeen_ = !away;
      windowChosen_ = false;
      syncLsb_ = v;
    }
    syncLsb_ = (uint16_t)(((uint32_t)syncLsb_ * 3 + v) / 4);
    prevLight_ = true;
    lastLitStart_ = st;
    const uint32_t lit = st - riseStart_;
    if (!windowChosen_ && lit > 2000) chooseWindow();  // 2 ms into the SYNC
    if (lit > kAmbientAdoptUs) {
      // Steady light (a lamp was switched on): it becomes the new dark level.
      setDark(syncLsb_);
      prevLight_ = false;
    }
    if (lit > kSyncMaxUs) phy_.idle();
    return Event::None;
  }
  if (prevLight_) {
    prevLight_ = false;
    const uint32_t width = st - riseStart_;
    // A SYNC seen from its start must be long enough to rule out mains hum; one
    // that was already on when listening resumed has only been seen in part.
    const uint32_t minWidth = riseSeen_ ? kSyncMinUs : kSyncMinUnseenUs;
    if (width >= minWidth && width <= kSyncMaxUs && windowChosen_) {
      ++stats_.syncs;
      const Event ev = receiveFrame(lastLitStart_, st, width);
      // Receiving a frame is not a pause in listening. Only a transmission is,
      // and the ACK sent from receiveFrame() has already marked it.
      lastRxEndUs_ = phy_.micros();
      lastListenUs_ = lastRxEndUs_;
      if (frameHandler_) frameHandler_(frame_);
      return ev;
    }
    if (width >= kSyncMinUs / 2) ++stats_.syncRejected;
  } else {
    // At rest the dark level and its noise follow the ambient light.
    const uint16_t dev = v > darkLsb_ ? v - darkLsb_ : darkLsb_ - v;
    noiseLsb_ = (uint16_t)((noiseLsb_ * 15u + dev) / 16u);
    setDark((uint16_t)((darkLsb_ * 15u + v) / 16u));
  }
  phy_.idle();
  return Event::None;
}

// Integration window for the frame: aims at a light reading of kTargetLevelLsb,
// away from the ADC dead zone and from saturation (ambient light included).
void PacketLED::chooseWindow() {
  uint32_t t = kCalWindowUs, st = 0;
  uint16_t v = phy_.integrate(t, st);
  while (v < kTargetLevelLsb / 4 && t < maxWindowUs_) {
    t = t * 4 < maxWindowUs_ ? t * 4 : maxWindowUs_;
    v = phy_.integrate(t, st);
  }
  uint32_t w = v ? (uint32_t)((uint64_t)t * kTargetLevelLsb / v) : maxWindowUs_;
  if (w < kMinWindowUs) w = kMinWindowUs;
  if (w > maxWindowUs_) w = maxWindowUs_;
  frameWindowUs_ = w;
  windowChosen_ = true;
}

// Bit phase from the preamble (light, dark, light, dark...). Pair k measures at
// start + k * (period + period / 8) and half a bit later. The first harmonic of
// the 8 differences gives the phase; its magnitude is the signal level.
bool PacketLED::sweepPhase(uint32_t start, uint32_t &gridStart) {
  const uint32_t period = 2 * slotUs_;
  float c = 0, s = 0;
  for (uint8_t k = 0; k < kSweepPairs; ++k) {
    const uint32_t t = start + k * period + k * period / kSweepPairs;
    uint32_t st = 0;
    const int32_t a = integrateAt(t, st);
    const int32_t b = integrateAt(t + slotUs_, st);
    const float ang = kTwoPi * k / kSweepPairs;
    c += (a - b) * cosf(ang);
    s += (a - b) * sinf(ang);
  }
  const float amp = sqrtf(c * c + s * s) * 2 / kSweepPairs;
  sigLevel_ = amp > 65535 ? 65535 : (uint16_t)amp;
  frame_.levelLsb = sigLevel_;
  if (amp < kMinSignalLsb) return false;
  float ang = atan2f(s, c);
  if (ang < 0) ang += kTwoPi;
  const uint32_t phase = (uint32_t)(ang / kTwoPi * period);
  frame_.phaseUs = (int32_t)phase;
  gridStart = start + (kSweepPairs + 1) * period + phase;
  return true;
}

// Light in the first half -> 0, in the second half -> 1. Returns the margin.
int32_t PacketLED::readBit(uint32_t start, uint8_t &bit) {
  uint32_t st = 0;
  const int32_t a = integrateAt(start, st);
  const int32_t b = integrateAt(start + slotUs_, st);
  bit = a < b ? 1 : 0;
  return a > b ? a - b : b - a;
}

PacketLED::Event PacketLED::receiveFrame(uint32_t lastLitStart, uint32_t darkStart, uint32_t width) {
  FrameInfo &f = frame_;
  f = FrameInfo{};
  f.minMarginLsb = INT32_MAX;
  f.syncWidthUs = width;
  f.windowUs = frameWindowUs_;

  // The SYNC ended after the start of the last light reading and before the end
  // of the first dark one: start from the middle of that interval.
  const uint32_t span = darkStart + listenUs_ - lastLitStart;
  uint32_t err = span / 2;
  if (err > kMaxSyncErrUs) err = kMaxSyncErrUs;
  f.syncErrUs = (int32_t)err;
  const uint32_t preamble = lastLitStart + span / 2 + kGuardUs;

  uint32_t t = 0;
  if (!sweepPhase(preamble + err, t)) return fail(Result::NoSignal);
  const uint32_t period = 2 * slotUs_;

  uint8_t sr = 0, bit = 0;
  bool found = false;
  for (uint8_t i = 0; i < kSfdSearchBits; ++i, t += period) {
    readBit(t, bit);
    sr = (uint8_t)((sr << 1) | bit);
    if (i >= 7 && sr == kSfd) {
      f.sfdBit = i;
      found = true;
      t += period;
      break;
    }
  }
  if (!found) return fail(Result::NoSfd);

  auto readByte = [&]() {
    uint8_t v = 0;
    for (uint8_t b = 0; b < 8; ++b, t += period) {
      const int32_t margin = readBit(t, bit);
      v = (uint8_t)((v << 1) | bit);
      if (margin < f.minMarginLsb) f.minMarginLsb = margin;
      if (margin < (int32_t)sigLevel_ / 4) ++f.weakBits;
      ++f.bits;
    }
    return v;
  };

  uint8_t buf[kHeaderLen + kMaxPayload + kFcsLen];
  for (uint8_t i = 0; i < kHeaderLen; ++i) buf[i] = readByte();
  f.type = buf[0];
  f.session = buf[1];
  f.seq = buf[2];
  f.len = buf[3];
  if ((f.type != kTypeData && f.type != kTypeAck) || f.len > kMaxPayload || (f.type == kTypeAck && f.len != 0))
    return fail(Result::BadHeader);
  for (uint8_t i = 0; i < f.len + kFcsLen; ++i) buf[kHeaderLen + i] = readByte();

  // Measurements are over: processing only from here on.
  const uint16_t fcs = (uint16_t)(buf[kHeaderLen + f.len] | buf[kHeaderLen + f.len + 1] << 8);
  if (fcs16(buf, kHeaderLen + f.len) != fcs) return fail(Result::BadCrc);
  f.result = Result::Ok;
  ++stats_.framesOk;
  if (f.minMarginLsb < stats_.minMarginLsb) stats_.minMarginLsb = f.minMarginLsb;
  if (f.levelLsb < stats_.minLevelLsb) stats_.minLevelLsb = f.levelLsb;
  return handleFrame(f.type, f.session, f.seq, buf + kHeaderLen, f.len);
}

PacketLED::Event PacketLED::fail(Result r) {
  frame_.result = r;
  switch (r) {
    case Result::NoSignal: ++stats_.noSignal; break;
    case Result::NoSfd: ++stats_.noSfd; break;
    case Result::BadHeader: ++stats_.headerErrors; break;
    case Result::BadCrc: ++stats_.crcErrors; break;
    case Result::Ok: break;
  }
  prevLight_ = false;
  return Event::Bad;
}

PacketLED::Event PacketLED::handleFrame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t *data,
                                          uint8_t len) {
  prevLight_ = false;
  if (type == kTypeAck) {
    if (waitingAck_ && session == session_ && seq == wantedSeq_) ackMatched_ = true;
    else ++stats_.acksIgnored;
    return Event::Ack;
  }
  // A restart of the sender changes its session, so session + seq identifies a packet.
  const bool duplicate = lastDataValid_ && session == lastDataSession_ && seq == lastDataSeq_;
  if (duplicate) {
    ++stats_.duplicates;
  } else if (pending_) {
    // The previous packet has not been collected by parsePacket() yet: no ACK,
    // so the sender will retransmit.
    ++stats_.rxOverruns;
    return Event::Bad;
  } else {
    lastDataValid_ = true;
    lastDataSession_ = session;
    lastDataSeq_ = seq;
    memcpy(rxBuf_, data, len);
    rxLen_ = len;
    rxPos_ = 0;
    rxSession_ = session;
    rxSeq_ = seq;
    rxLevel_ = frame_.levelLsb;
    rxMargin_ = frame_.minMarginLsb;
    pending_ = true;
  }
  // Duplicates are acknowledged too: the previous ACK may have been lost.
  phy_.delayMs(kTurnaroundMs);
  transmit(kTypeAck, session, seq, nullptr, 0);
  return duplicate ? Event::Duplicate : Event::NewData;
}

const char *PacketLED::resultText(Result r) {
  switch (r) {
    case Result::Ok: return "OK";
    case Result::NoSignal: return "no signal";
    case Result::NoSfd: return "no start delimiter";
    case Result::BadHeader: return "bad header";
    case Result::BadCrc: return "bad CRC";
  }
  return "?";
}
