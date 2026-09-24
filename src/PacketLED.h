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

#ifndef PACKETLED_H
#define PACKETLED_H

#include <stddef.h>
#include <stdint.h>

#include "LedPhy.h"

#ifdef ARDUINO
#include <Arduino.h>
#define PACKETLED_BASE : public Stream
#define PACKETLED_OVERRIDE override
#else
#define PACKETLED_BASE
#define PACKETLED_OVERRIDE
#endif

#define PACKETLED_VERSION "1.0.2"

/*
 * LX.25 frame (Manchester bits, MSB first: 0 = light then dark, 1 = dark then light):
 *
 *   SYNC      20 ms of light; the receiver picks its integration window here
 *   GUARD     3 ms of dark
 *   PREAMBLE  16 zero bits; the receiver measures its bit phase
 *   SFD       0xA7
 *   HEADER    type, session, sequence, length
 *   PAYLOAD   0-64 bytes
 *   FCS       CRC-16/X.25 over header and payload (the AX.25 FCS), low byte first
 *
 * All the constants below are part of the protocol: both sides must use the
 * same values.
 */
namespace lx25 {

constexpr uint8_t kMaxPayload = 64;
constexpr uint32_t kDefaultBitRate = 1024;
constexpr uint32_t kMinBitRate = 256;
constexpr uint32_t kMaxBitRate = 1024;

constexpr uint32_t kSyncUs = 20000;
constexpr uint32_t kSyncMinUs = 17000;         // well above a 50 Hz half-wave (10 ms)
constexpr uint32_t kSyncMinUnseenUs = 5000;    // when the start of the light was not seen
constexpr uint32_t kSyncMaxUs = 35000;
constexpr uint32_t kGuardUs = 3000;
constexpr uint32_t kListenWindowUs = 600;      // minimum; longer at low rates (see begin())
constexpr uint32_t kMaxListenWindowUs = 1200;
constexpr uint32_t kListenGapUs = 3000;        // a longer pause between two readings: the listener was away
constexpr uint16_t kMinLightLsb = 60;          // SYNC detection: minimum rise over the dark level
constexpr uint8_t kNoiseFactor = 3;            // ... and at least this many times the resting noise
constexpr uint32_t kAmbientAdoptUs = 100000;   // steady light longer than this becomes the new dark level

constexpr uint16_t kTargetLevelLsb = 2000;     // integration target: far from zero and from saturation
constexpr uint32_t kMinWindowUs = 20;
constexpr uint32_t kCalWindowUs = 40;

constexpr uint8_t kPreambleBits = 16;
constexpr uint8_t kSweepPairs = 8;
constexpr uint8_t kSfd = 0xA7;
constexpr uint8_t kSfdSearchBits = 16;
constexpr uint32_t kMaxSyncErrUs = 1500;
constexpr uint16_t kMinSignalLsb = 30;

constexpr uint8_t kMaxRetries = 3;
constexpr uint32_t kTurnaroundMs = 20;
constexpr uint32_t kTxGapMs = 10;              // pause after the end of a received frame before sending
constexpr uint32_t kAckTimeoutMs = 400;
constexpr uint8_t kTypeData = 0x44;
constexpr uint8_t kTypeAck = 0x41;

}  // namespace lx25

class PacketLED PACKETLED_BASE {
 public:
  /** Outcome of a received frame. */
  enum class Result : uint8_t {
    Ok,
    NoSignal,   // preamble missing or too weak
    NoSfd,      // preamble found, start delimiter not found
    BadHeader,
    BadCrc,
  };

  /** Diagnostics of the last received frame, including rejected ones. */
  struct FrameInfo {
    Result result;
    uint8_t type, session, seq, len;
    uint32_t syncWidthUs;    // SYNC length as seen by the listener
    uint32_t windowUs;       // integration window used for this frame
    int32_t syncErrUs;       // uncertainty (+/-) on the end of the SYNC
    int32_t phaseUs;         // bit phase measured on the preamble
    uint16_t levelLsb;       // signal amplitude (light - dark)
    uint8_t sfdBit;          // bits read after the phase sweep before the SFD
    uint16_t bits;           // bits read after the SFD
    uint16_t weakBits;       // bits with less than 1/4 of the signal amplitude
    int32_t minMarginLsb;    // smallest difference between the two halves of a bit
    int32_t lateMaxUs;       // worst measurement start delay
  };

  struct Stats {
    uint32_t syncs, syncRejected;
    uint32_t framesOk, noSignal, noSfd, headerErrors, crcErrors;
    uint32_t duplicates, acksIgnored, rxOverruns;
    uint32_t sent, sentFirstTry, retransmissions, sendFailed;
    int32_t minMarginLsb;    // over valid frames
    uint16_t minLevelLsb;
  };

  explicit PacketLED(LedPhy &phy);

  /**
   * Starts the link. Call it while the LED of the other board is off: the
   * resting dark level is measured here.
   *
   * @param bitRate 256-1024 bit/s, the same on both sides. Lower rates reach
   *                further.
   * @return false if the bit rate is out of range or too fast for the board;
   *         the previous settings are then kept.
   */
  bool begin(uint32_t bitRate = lx25::kDefaultBitRate);
  void end();

  /** Starts a new outgoing packet. Always returns 1. */
  int beginPacket();

  /**
   * Sends the packet built with write() / print().
   *
   * @param confirmed true: waits for the acknowledgement and retransmits up to
   *                  3 times. false: sends once.
   * @return true if acknowledged (or sent, when not confirmed); false if not
   *         acknowledged or if more than 64 bytes were written.
   */
  bool endPacket(bool confirmed = true);

  size_t write(uint8_t b) PACKETLED_OVERRIDE;
  size_t write(const uint8_t *buf, size_t n) PACKETLED_OVERRIDE;
#ifdef ARDUINO
  using Print::write;
#endif

  /** beginPacket() + write() + endPacket(true). */
  bool send(const uint8_t *data, size_t len);
  bool send(const char *text);

  /**
   * Listens for incoming packets. Call it continuously from loop().
   *
   * A new packet is acknowledged automatically; duplicates are acknowledged
   * again but not returned. Blocks while a frame is being received.
   *
   * @return size of the new packet, 0 if none. Use packetAvailable() to tell
   *         an empty packet from no packet.
   */
  int parsePacket();
  bool packetAvailable() const { return rxReady_; }
  int available() PACKETLED_OVERRIDE;
  int read() PACKETLED_OVERRIDE;
  int peek() PACKETLED_OVERRIDE;
  void flush() PACKETLED_OVERRIDE {}

  uint8_t packetSession() const { return rxSession_; }
  uint8_t packetSeq() const { return rxSeq_; }
  uint16_t packetLevel() const { return rxLevel_; }
  int32_t packetMargin() const { return rxMargin_; }

  /**
   * Registers a callback invoked after every received frame, rejected ones
   * included. Do not call endPacket(), send() or parsePacket() from it.
   */
  void onFrame(void (*handler)(const FrameInfo &)) { frameHandler_ = handler; }
  const FrameInfo &lastFrame() const { return frame_; }
  const Stats &stats() const { return stats_; }
  void resetStats();

  uint8_t session() const { return session_; }
  uint8_t lastAttempts() const { return lastAttempts_; }
  uint32_t bitRate() const { return bitRate_; }
  uint32_t maxWindowUs() const { return maxWindowUs_; }
  uint16_t darkLevel() const { return darkLsb_; }
  uint16_t noiseLevel() const { return noiseLsb_; }

  /** Single raw reading with the given integration time (0-4095). */
  uint16_t measureLight(uint32_t windowUs);
  void setLed(bool on);

  static const char *resultText(Result r);

 private:
  enum class Event : uint8_t { None, NewData, Duplicate, Ack, Bad };

  Event listenOnce();
  void setDark(uint16_t dark);
  void chooseWindow();
  Event receiveFrame(uint32_t lastLitStart, uint32_t darkStart, uint32_t width);
  bool sweepPhase(uint32_t start, uint32_t &gridStart);
  int32_t readBit(uint32_t start, uint8_t &bit);
  Event fail(Result r);
  Event handleFrame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t *data, uint8_t len);
  bool transmitPacket(bool confirmed);
  void transmit(uint8_t type, uint8_t session, uint8_t seq, const uint8_t *data, uint8_t len);
  uint16_t integrateAt(uint32_t startAt, uint32_t &startUs);
  void waitUntil(uint32_t t);

  LedPhy &phy_;
  uint32_t bitRate_ = lx25::kDefaultBitRate;
  uint32_t slotUs_ = 500000 / lx25::kDefaultBitRate;
  uint32_t setupUs_ = 0;
  uint32_t maxWindowUs_ = 0;
  uint32_t listenUs_ = lx25::kListenWindowUs;

  uint16_t darkLsb_ = 0;
  uint16_t noiseLsb_ = 0;
  uint16_t lightThrLsb_ = lx25::kMinLightLsb;
  uint16_t syncLsb_ = 0;
  bool prevLight_ = false;
  bool windowChosen_ = false;
  uint32_t riseStart_ = 0, lastLitStart_ = 0;
  uint32_t lastListenUs_ = 0, lastRxEndUs_ = 0;
  bool resumed_ = false, riseSeen_ = true;
  uint32_t frameWindowUs_ = 0;

  uint8_t txBuf_[lx25::kMaxPayload] = {};
  uint8_t txLen_ = 0;
  bool txOverflow_ = false;
  uint8_t session_ = 0;
  uint8_t nextSeq_ = 1;
  bool waitingAck_ = false, ackMatched_ = false;
  uint8_t wantedSeq_ = 0;
  uint8_t lastAttempts_ = 0;

  uint8_t rxBuf_[lx25::kMaxPayload] = {};
  uint8_t rxLen_ = 0, rxPos_ = 0;
  bool rxReady_ = false, pending_ = false;
  uint8_t rxSession_ = 0, rxSeq_ = 0;
  uint16_t rxLevel_ = 0;
  int32_t rxMargin_ = 0;
  bool lastDataValid_ = false;
  uint8_t lastDataSession_ = 0, lastDataSeq_ = 0;

  uint16_t sigLevel_ = 0;

  FrameInfo frame_ = {};
  Stats stats_ = {};
  void (*frameHandler_)(const FrameInfo &) = nullptr;
};

#endif  // PACKETLED_H
